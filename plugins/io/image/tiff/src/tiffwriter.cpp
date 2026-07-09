#include "lain/io/image/save.h" // ImageWriter, writerRegistry
#include "lain/io/image/writer.h"

#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/memory/buffer.h>
#include <tiffio.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace lain::io::image::tiff
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A growable in-memory TIFF file for libtiff's client I/O. Writing a TIFF isn't
	// append-only — libtiff seeks back to patch the IFD offset and reads during finalise — so
	// this supports read/write/seek over a std::vector. libtiff has no longjmp (error handlers
	// return), so the vector is safe as a local.
	struct MemFile
	{
		std::vector<std::uint8_t> data;
		toff_t offset = 0;
	};

	static tmsize_t memRead(thandle_t handle, void* buffer, tmsize_t count)
	{
		auto* file = static_cast<MemFile*>(handle);
		if (file->offset >= static_cast<toff_t>(file->data.size()))
			return 0;
		const tmsize_t available = static_cast<tmsize_t>(static_cast<toff_t>(file->data.size()) - file->offset);
		const tmsize_t take = count < available ? count : available;
		std::memcpy(buffer, file->data.data() + file->offset, static_cast<std::size_t>(take));
		file->offset += take;
		return take;
	}

	static tmsize_t memWrite(thandle_t handle, void* buffer, tmsize_t count)
	{
		auto* file = static_cast<MemFile*>(handle);
		const std::size_t end = static_cast<std::size_t>(file->offset) + static_cast<std::size_t>(count);
		if (end > file->data.size())
			file->data.resize(end); // grow (a seek past the end zero-fills the gap)
		std::memcpy(file->data.data() + static_cast<std::size_t>(file->offset), buffer, static_cast<std::size_t>(count));
		file->offset += count;
		return count;
	}

	static toff_t memSeek(thandle_t handle, toff_t offset, int whence)
	{
		auto* file = static_cast<MemFile*>(handle);
		const toff_t base =
			whence == SEEK_CUR ? file->offset : (whence == SEEK_END ? static_cast<toff_t>(file->data.size()) : 0);
		file->offset = base + offset;
		return file->offset;
	}

	static int memClose(thandle_t) { return 0; }
	static toff_t memSize(thandle_t handle) { return static_cast<toff_t>(static_cast<MemFile*>(handle)->data.size()); }
	static int memMap(thandle_t, void**, toff_t*) { return 0; }
	static void memUnmap(thandle_t, void*, toff_t) {}

	// The TIFF sample layout for a lain PixelFormat. Returns false for a format this writer
	// won't store — float (matches the reader, which reads only integer; TIFF float is a
	// deferred "one day" — WORK.md M3).
	static bool tiffTypeFor(lain::image::PixelFormat format, int& bits, int& samples, int& photometric, bool& hasAlpha)
	{
		const auto desc = lain::image::descriptor(format);
		if (desc.channelType == lain::image::ChannelType::F32)
			return false;
		bits = desc.channelType == lain::image::ChannelType::U16 ? 16 : 8;
		samples = desc.channelCount();
		hasAlpha = desc.hasAlpha();
		switch (desc.model)
		{
			case lain::image::ColorModel::Gray:
			case lain::image::ColorModel::GrayAlpha:
				photometric = PHOTOMETRIC_MINISBLACK;
				return true;
			case lain::image::ColorModel::RGB:
			case lain::image::ColorModel::RGBA:
				photometric = PHOTOMETRIC_RGB;
				return true;
		}
		return false;
	}

	// The TIFF writer: a CPU image::Image in, TIFF bytes out. Stateless. Writes the common
	// striped/chunky/integer layout the reader reads back — Gray/GrayAlpha/RGB/RGBA at 8/16-bit,
	// AlphaMode carried via ExtraSamples. libtiff manages byte order (the file header records
	// host order), so 16-bit needs no manual swap. Compression is LZW (lossless, core libtiff,
	// no external dep); a compression-type knob is deferred (WORK.md M3: encoder config).
	class TiffWriter : public ImageWriter
	{
	public:
		bool canEncode(const lain::image::Image& image) const override
		{
			// This writer stores 8/16-bit integer Gray/GrayAlpha/RGB/RGBA (float is deferred).
			return image.valid() && image.descriptor().channelType != lain::image::ChannelType::F32;
		}

		std::optional<memory::Buffer> encode(const lain::image::Image& image) const override
		{
			if (!image.valid())
				return std::nullopt;

			int bits = 0;
			int samples = 0;
			int photometric = 0;
			bool hasAlpha = false;
			if (!tiffTypeFor(image.pixelFormat(), bits, samples, photometric, hasAlpha))
				return std::nullopt;

			MemFile sink;
			TIFF* tif = TIFFClientOpen("lain-mem", "w", &sink, memRead, memWrite, memSeek, memClose, memSize, memMap,
									   memUnmap);
			if (tif == nullptr)
				return std::nullopt;

			TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(image.width()));
			TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(image.height()));
			TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bits);
			TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, samples);
			TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, photometric);
			TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
			TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
			TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
			TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
			TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
			if (hasAlpha)
			{
				const std::uint16_t extra =
					image.alphaMode() == lain::image::AlphaMode::Premultiplied ? EXTRASAMPLE_ASSOCALPHA
																			   : EXTRASAMPLE_UNASSALPHA;
				TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, 1, &extra);
			}

			// Write scanlines from a per-row scratch copy: TIFFWriteScanline may modify the
			// buffer it's given (predictors), so it must not point into the caller's image.
			const std::size_t rowBytes = image.byteSize() / static_cast<std::size_t>(image.height());
			std::vector<std::uint8_t> row(rowBytes);
			bool ok = true;
			for (int y = 0; y < image.height() && ok; ++y)
			{
				std::memcpy(row.data(), image.data() + static_cast<std::size_t>(y) * rowBytes, rowBytes);
				if (TIFFWriteScanline(tif, row.data(), static_cast<std::uint32_t>(y), 0) < 0)
					ok = false;
			}

			TIFFClose(tif); // flushes the IFD into sink.data
			if (!ok)
				return std::nullopt;

			memory::Buffer buffer(sink.data.size());
			std::memcpy(buffer.data(), sink.data.data(), sink.data.size());
			return buffer;
		}
	};

	// Register the TIFF writer into the writer registry. Called by registerCodec (tiffreader.cpp)
	// so the codec's reader + writer register together.
	void registerTiffWriter()
	{
		writerRegistry().registerType<TiffWriter>("tiff");
		writerRegistry().registerType<TiffWriter>("tif");
	}
} // namespace lain::io::image::tiff
