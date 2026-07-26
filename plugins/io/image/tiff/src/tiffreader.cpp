#include "lain/io/image/tiff/register.h"

#include <lain/image/image.h>
#include <lain/io/image/load.h>	  // readerRegistry
#include <lain/io/image/reader.h> // ImageReader
#include <lain/log/log.h>
#include <lain/memory/buffer.h>

#include <tiffio.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lain::io::image::tiff
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The lain PixelFormat for a channel count (1/2/3/4 -> Gray/GrayAlpha/RGB/RGBA) at 8-
	// or 16-bit depth. (TIFF-side; mirrors the PNG plugin's mapping — kept per-plugin so
	// the io::image core stays codec-agnostic.)
	static lain::image::PixelFormat pixelFormatFor(int channels, int bitDepth)
	{
		const bool is16 = bitDepth == 16;
		switch (channels)
		{
			case 1:
				return is16 ? lain::image::PixelFormat::Gray16 : lain::image::PixelFormat::Gray8;
			case 2:
				return is16 ? lain::image::PixelFormat::GrayAlpha16 : lain::image::PixelFormat::GrayAlpha8;
			case 3:
				return is16 ? lain::image::PixelFormat::RGB16 : lain::image::PixelFormat::RGB8;
			default:
				return is16 ? lain::image::PixelFormat::RGBA16 : lain::image::PixelFormat::RGBA8;
		}
	}

	// libtiff's error/warning handlers are process-global and default to stderr; route them
	// to lain::log at debug level so a rejected TIFF doesn't spam stdout/stderr.
	static void tiffMessageToLog(const char* module, const char* fmt, va_list ap)
	{
		char message[256];
		std::vsnprintf(message, sizeof(message), fmt, ap);
		lain::log::debug("libtiff [{}]: {}", module != nullptr ? module : "", message);
	}

	// A read cursor over the encoded bytes, handed to libtiff as its client I/O handle.
	struct MemSource
	{
		const std::uint8_t* data;
		toff_t size;
		toff_t offset;
	};

	static tmsize_t memRead(thandle_t handle, void* buffer, tmsize_t count)
	{
		auto* source = static_cast<MemSource*>(handle);
		if (source->offset >= source->size)
			return 0;
		const tmsize_t available = static_cast<tmsize_t>(source->size - source->offset);
		const tmsize_t take = count < available ? count : available;
		std::memcpy(buffer, source->data + source->offset, static_cast<std::size_t>(take));
		source->offset += take;
		return take;
	}

	static tmsize_t memWrite(thandle_t, void*, tmsize_t) { return 0; } // read-only

	static toff_t memSeek(thandle_t handle, toff_t offset, int whence)
	{
		auto* source = static_cast<MemSource*>(handle);
		const toff_t base = whence == SEEK_CUR ? source->offset : (whence == SEEK_END ? source->size : 0);
		source->offset = base + offset;
		return source->offset;
	}

	static int memClose(thandle_t) { return 0; }
	static toff_t memSize(thandle_t handle) { return static_cast<MemSource*>(handle)->size; }
	static int memMap(thandle_t, void**, toff_t*) { return 0; } // no memory mapping
	static void memUnmap(thandle_t, void*, toff_t) {}

	// Decode TIFF bytes directly into `out`. TIFF is a container with many orthogonal axes;
	// this reader covers the common, faithfully-representable subset and REJECTS the rest
	// loudly (logs, returns false) rather than silently degrading it:
	//
	//   bit depth      8 / 16-bit            (reject 1/2/4-bit fax/bilevel, 32-bit)
	//   sample format  unsigned integer      (reject float, signed)
	//   photometric    grayscale, RGB        (reject MINISWHITE, palette, CMYK, YCbCr, CIELab)
	//   samples/pixel  1-4 (Gray/GA/RGB/RGBA)(reject >4 multispectral)
	//   planar config  chunky / contig       (reject planar / separate)
	//   layout         striped               (reject tiled)
	//   compression    whatever this libtiff build decodes (none/LZW/Deflate/PackBits, not
	//                  JPEG-in-TIFF/LZMA/ZSTD/WebP — a decode failure then rejects)
	//
	// The covered subset is preserved exactly, incl. the 16-bit (U16) path; AlphaMode is read
	// from ExtraSamples (TIFF distinguishes premultiplied vs straight). ColorSpace is always
	// Unspecified — TIFF's colour is ICC/colorimetry, which lain can't yet represent. Two
	// things decode but are NOT honoured, each with a logged warning: a multi-page TIFF reads
	// only its first page, and a non-default Orientation tag is ignored (rows read as stored).
	// "One day" (WORK.md M3): float sample format (intermediary files), CIELab, YCbCr.
	static bool decodeTiffInto(const std::uint8_t* bytes, std::size_t length, lain::image::Image& out)
	{
		MemSource source{bytes, static_cast<toff_t>(length), 0};
		TIFF* tif = TIFFClientOpen("lain-mem", "r", &source, memRead, memWrite, memSeek, memClose, memSize,
								   memMap, memUnmap);
		if (tif == nullptr)
			return false;

		if (TIFFNumberOfDirectories(tif) > 1)
			lain::log::warn("io::image tiff: multi-page TIFF — reading the first page only");
		TIFFSetDirectory(tif, 0); // ensure page 0 (TIFFNumberOfDirectories may walk the chain)

		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint16_t bits = 0;
		std::uint16_t samples = 0;
		std::uint16_t photometric = 0;
		std::uint16_t planar = PLANARCONFIG_CONTIG;
		std::uint16_t sampleFormat = SAMPLEFORMAT_UINT;
		std::uint16_t orientation = ORIENTATION_TOPLEFT;
		TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
		TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
		TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bits);
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples);
		TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
		TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orientation);
		const bool havePhotometric = TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric) != 0;

		const bool supported = havePhotometric && !TIFFIsTiled(tif) && planar == PLANARCONFIG_CONTIG &&
							   sampleFormat == SAMPLEFORMAT_UINT && (bits == 8 || bits == 16) && samples >= 1 &&
							   samples <= 4 &&
							   (photometric == PHOTOMETRIC_MINISBLACK || photometric == PHOTOMETRIC_RGB) &&
							   width > 0 && height > 0;
		if (!supported)
		{
			lain::log::warn("io::image tiff: unsupported layout (tiled={} planar={} sampleFormat={} bits={} "
							"samples={} photometric={})",
							TIFFIsTiled(tif) != 0, planar, sampleFormat, bits, samples, photometric);
			TIFFClose(tif);
			return false;
		}

		if (orientation != ORIENTATION_TOPLEFT)
			lain::log::warn("io::image tiff: Orientation {} ignored — pixels read as stored", orientation);

		// Alpha: TIFF's ExtraSamples distinguishes associated (premultiplied) from
		// unassociated (straight) — unlike PNG, which is always straight.
		auto alphaMode = lain::image::AlphaMode::Unspecified;
		if (samples == 2 || samples == 4)
		{
			alphaMode = lain::image::AlphaMode::Straight;
			std::uint16_t extraCount = 0;
			std::uint16_t* extra = nullptr;
			if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &extraCount, &extra) != 0 && extraCount > 0 &&
				extra != nullptr && extra[extraCount - 1] == EXTRASAMPLE_ASSOCALPHA)
				alphaMode = lain::image::AlphaMode::Premultiplied;
		}

		out = lain::image::Image(static_cast<int>(width), static_cast<int>(height), pixelFormatFor(samples, bits),
								 lain::image::ColorSpace::Unspecified, alphaMode);

		// Read scanlines straight into the Image. libtiff returns native-order samples, so
		// 16-bit data needs no manual swap. Its row size must equal the packed stride.
		const std::size_t stride = out.byteSize() / height;
		if (static_cast<std::size_t>(TIFFScanlineSize(tif)) != stride)
		{
			TIFFClose(tif);
			return false;
		}
		for (std::uint32_t row = 0; row < height; ++row)
		{
			if (TIFFReadScanline(tif, out.data() + static_cast<std::size_t>(row) * stride, row) < 0)
			{
				TIFFClose(tif);
				return false;
			}
		}

		TIFFClose(tif);
		return true;
	}

	// The TIFF reader: libtiff in, a CPU image::Image out. Stateless; the registry makes one
	// per decode.
	//
	// Format policy (see decodeTiffInto): the common striped/chunky/integer/8-16-bit/gray-rgb
	// layout is preserved exactly (Gray/GrayAlpha/RGB/RGBA, incl. the U16 path, with alpha
	// mode read from ExtraSamples). Unsupported layouts are rejected loudly, never silently
	// degraded. ColorSpace is Unspecified (TIFF's colour is ICC/colorimetry, which lain can't
	// yet represent — WORK.md M3).
	class TiffReader : public ImageReader
	{
	public:
		lain::image::Image decode(const memory::Buffer& bytes) const override
		{
			lain::image::Image image;
			if (!decodeTiffInto(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), image))
				return {}; // invalid Image -> the facade turns this into a logged nullopt
			return image;
		}
	};

	// Defined in tiffwriter.cpp (same plugin); registered together so the codec's reader and
	// writer arrive as a pair.
	void registerTiffWriter();

	void registerCodec()
	{
		TIFFSetErrorHandler(&tiffMessageToLog);
		TIFFSetWarningHandler(&tiffMessageToLog);
		readerRegistry().registerType<TiffReader>("tiff");
		readerRegistry().registerType<TiffReader>("tif");
		registerTiffWriter();
	}
} // namespace lain::io::image::tiff
