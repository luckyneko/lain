#include "lain/io/image/save.h" // ImageWriter, writerRegistry
#include "lain/io/image/writer.h"

#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/memory/buffer.h>

#include <png.h>

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace lain::io::image::png
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// PNG samples are big-endian on the wire; a little-endian host must byte-swap its 16-bit
	// data before writing (mirror of the reader). (Duplicated per TU — reader/writer are
	// separate translation units.)
	static bool hostIsLittleEndian()
	{
		const std::uint16_t probe = 0x0102;
		return *reinterpret_cast<const std::uint8_t*>(&probe) == 0x02;
	}

	// libpng write callback: append the encoded bytes to the std::vector passed as the io_ptr.
	// A growable vector accumulates the stream (the encoded size isn't known ahead); encode()
	// then copies it once into a fixed-size memory::Buffer — Buffer is deliberately
	// non-resizable, so the one copy at the boundary is inherent (a growable buffer builder
	// would belong with the deferred memory-pool work, not here).
	static void writeToVector(png_structp png, png_bytep data, png_size_t length)
	{
		auto* out = static_cast<std::vector<std::uint8_t>*>(png_get_io_ptr(png));
		out->insert(out->end(), data, data + length);
	}

	static void flushVector(png_structp) {}

	// The PNG colour type + bit depth for a lain PixelFormat. Returns false for a format PNG
	// can't store — float (PNG is 8/16-bit integer).
	static bool pngTypeFor(lain::image::PixelFormat format, int& colorType, int& bitDepth)
	{
		const auto desc = lain::image::descriptor(format);
		if (desc.channelType == lain::image::ChannelType::F32)
			return false;
		bitDepth = desc.channelType == lain::image::ChannelType::U16 ? 16 : 8;
		switch (desc.model)
		{
			case lain::image::ColorModel::Gray:
				colorType = PNG_COLOR_TYPE_GRAY;
				return true;
			case lain::image::ColorModel::GrayAlpha:
				colorType = PNG_COLOR_TYPE_GRAY_ALPHA;
				return true;
			case lain::image::ColorModel::RGB:
				colorType = PNG_COLOR_TYPE_RGB;
				return true;
			case lain::image::ColorModel::RGBA:
				colorType = PNG_COLOR_TYPE_RGB_ALPHA;
				return true;
		}
		return false;
	}

	// Encode `image` into `out` (which lives in the caller's frame, so a libpng longjmp — it
	// unwinds only to the setjmp below — never crosses it). Compression uses libpng's default;
	// a level knob is deferred (WORK.md M3: encoder config).
	static bool encodePngInto(const lain::image::Image& image, std::vector<std::uint8_t>& out)
	{
		int colorType = 0;
		int bitDepth = 0;
		if (!pngTypeFor(image.pixelFormat(), colorType, bitDepth))
			return false;

		png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		if (png == nullptr)
			return false;
		png_infop info = png_create_info_struct(png);
		if (info == nullptr)
		{
			png_destroy_write_struct(&png, nullptr);
			return false;
		}

		// The row-pointer array libpng wants (one entry per scanline) — the only allocation
		// across the setjmp. It is a raw malloc (no C++ destructor for a longjmp to skip) and
		// is null-initialised, so the ERROR HANDLER below — which runs only when a later
		// png_error longjmps *back* to this setjmp, not in program order — free()s it safely
		// whether or not the jump happened before it was allocated.
		png_bytep* rows = nullptr;
		if (setjmp(png_jmpbuf(png)) != 0)
		{
			std::free(rows);
			png_destroy_write_struct(&png, &info);
			return false;
		}

		png_set_write_fn(png, &out, &writeToVector, &flushVector);
		png_set_IHDR(png, info, static_cast<png_uint_32>(image.width()), static_cast<png_uint_32>(image.height()),
					 bitDepth, colorType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		png_write_info(png, info);
		if (bitDepth == 16 && hostIsLittleEndian())
			png_set_swap(png); // native u16 -> big-endian; libpng swaps its own row copy, not ours

		// Point the row pointers straight at the Image's tightly-packed bytes — libpng only
		// *reads* them on write (it copies each scanline internally before transforming), so no
		// pixel copy is needed and the const_cast is safe (the image is not modified).
		const std::size_t rowBytes = image.byteSize() / static_cast<std::size_t>(image.height());
		rows = static_cast<png_bytep*>(std::malloc(sizeof(png_bytep) * image.height()));
		if (rows == nullptr)
			png_error(png, "out of memory"); // longjmps to the cleanup above
		auto* base = const_cast<std::uint8_t*>(image.data());
		for (int y = 0; y < image.height(); ++y)
			rows[y] = base + static_cast<std::size_t>(y) * rowBytes;

		png_write_image(png, rows);
		png_write_end(png, nullptr);

		std::free(rows);
		png_destroy_write_struct(&png, &info);
		return true;
	}

	// The PNG writer: a CPU image::Image in, PNG bytes out. Stateless. Preserves the format
	// (Gray/GrayAlpha/RGB/RGBA at 8/16-bit); a float format is rejected (nullopt) — PNG is
	// integer-only.
	class PngWriter : public ImageWriter
	{
	public:
		bool canEncode(const lain::image::Image& image) const override
		{
			// PNG stores 8/16-bit Gray/GrayAlpha/RGB/RGBA — every lain format except float.
			return image.valid() && image.descriptor().channelType != lain::image::ChannelType::F32;
		}

		std::optional<memory::Buffer> encode(const lain::image::Image& image) const override
		{
			if (!image.valid())
				return std::nullopt;

			std::vector<std::uint8_t> bytes;
			if (!encodePngInto(image, bytes))
				return std::nullopt;

			memory::Buffer buffer(bytes.size());
			std::memcpy(buffer.data(), bytes.data(), bytes.size());
			return buffer;
		}
	};

	// Register the PNG writer into the writer registry. Called by registerCodec (pngreader.cpp)
	// so a plugin's reader + writer register together.
	void registerPngWriter()
	{
		writerRegistry().registerType<PngWriter>("png");
	}
} // namespace lain::io::image::png
