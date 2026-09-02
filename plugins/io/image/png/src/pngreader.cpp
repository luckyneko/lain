#include "pngreader.h"

#include <lain/image/image.h>
#include <lain/io/image/load.h>	  // readerRegistry
#include <lain/io/image/reader.h> // ImageReader
#include <lain/memory/buffer.h>

#include <png.h>

#include <csetjmp>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace lain::io::image::png
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// True if this host stores multi-byte integers little-endian — PNG samples are
	// big-endian on the wire, so 16-bit data must be byte-swapped to native here.
	static bool hostIsLittleEndian()
	{
		const std::uint16_t probe = 0x0102;
		return *reinterpret_cast<const std::uint8_t*>(&probe) == 0x02;
	}

	// A cursor over the in-memory encoded bytes, handed to libpng as its read source.
	struct MemSource
	{
		const std::uint8_t* data;
		std::size_t size;
		std::size_t offset;
	};

	// The image's ColorSpace as declared by its PNG chunks — honestly, without guessing.
	// An sRGB chunk is definitive; a gAMA near 1/2.2 or 1.0 maps to sRGB / Linear; an ICC
	// profile, an unrecognised gamma, or no colour chunk at all is Unspecified (lain can't
	// represent an arbitrary profile, and "untagged" genuinely means unknown — the caller
	// declares/convert()s before a space-sensitive op, per the image contract).
	static lain::image::ColorSpace colorSpaceFromPng(png_structp png, png_infop info)
	{
		int srgbIntent = 0;
		if (png_get_sRGB(png, info, &srgbIntent) != 0)
			return lain::image::ColorSpace::sRGB;

		png_charp iccName = nullptr;
		int iccCompression = 0;
		png_bytep iccProfile = nullptr;
		png_uint_32 iccLength = 0;
		if (png_get_iCCP(png, info, &iccName, &iccCompression, &iccProfile, &iccLength) != 0)
			return lain::image::ColorSpace::Unspecified;

		double fileGamma = 0.0;
		if (png_get_gAMA(png, info, &fileGamma) != 0)
		{
			if (fileGamma > 0.44 && fileGamma < 0.47) // ~1/2.2, sRGB-ish encoding
				return lain::image::ColorSpace::sRGB;
			if (fileGamma > 0.99 && fileGamma < 1.01) // linear light
				return lain::image::ColorSpace::Linear;
			return lain::image::ColorSpace::Unspecified; // some other gamma lain can't track
		}
		return lain::image::ColorSpace::Unspecified; // untagged -> honestly unknown
	}

	static void readFromMemory(png_structp png, png_bytep out, png_size_t count)
	{
		auto* source = static_cast<MemSource*>(png_get_io_ptr(png));
		if (source->offset + count > source->size)
		{
			png_error(png, "read past end of buffer"); // longjmps; does not return
			return;
		}
		std::memcpy(out, source->data + source->offset, count);
		source->offset += count;
	}

	// The lain PixelFormat for a normalised channel count (1/2/3/4 -> Gray/GrayAlpha/RGB/
	// RGBA) at 8- or 16-bit depth. Grayscale and gray+alpha are kept, not expanded to RGB.
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

	// Decode PNG bytes straight into `out` — no intermediate pixel buffer. The destination
	// Image lives in the CALLER's frame, so a libpng longjmp (which unwinds only to the
	// setjmp below) never crosses it: `out` is assigned here but destroyed by the caller,
	// leak-free. The only local across the setjmp is a malloc'd row-pointer array (C-style,
	// no destructor to skip). Returns false on any libpng error, leaving `out` to the caller
	// to discard.
	static bool decodePngInto(const std::uint8_t* bytes, std::size_t length, lain::image::Image& out)
	{
		if (length < 8 || png_sig_cmp(bytes, 0, 8) != 0)
			return false;

		png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		if (png == nullptr)
			return false;
		png_infop info = png_create_info_struct(png);
		if (info == nullptr)
		{
			png_destroy_read_struct(&png, nullptr, nullptr);
			return false;
		}

		png_bytep* rows = nullptr;
		if (setjmp(png_jmpbuf(png)) != 0)
		{
			std::free(rows);
			png_destroy_read_struct(&png, &info, nullptr);
			return false;
		}

		MemSource source{bytes, length, 0};
		png_set_read_fn(png, &source, &readFromMemory);
		png_read_info(png, info);

		png_uint_32 w = 0;
		png_uint_32 h = 0;
		int depth = 0;
		int colorType = 0;
		png_get_IHDR(png, info, &w, &h, &depth, &colorType, nullptr, nullptr, nullptr);
		const lain::image::ColorSpace colorSpace = colorSpaceFromPng(png, info);

		// Normalise into a lain-representable format, expanding only what lain can't hold
		// (documented lossless expansion; see the class note). Grayscale and gray+alpha are
		// PRESERVED — lain has Gray8/16 and GrayAlpha8/16 — so no gray->rgb here.
		if (colorType == PNG_COLOR_TYPE_PALETTE)
			png_set_palette_to_rgb(png); // indexed -> RGB (lain has no palette format)
		if (colorType == PNG_COLOR_TYPE_GRAY && depth < 8)
			png_set_expand_gray_1_2_4_to_8(png); // sub-byte -> 8-bit (lain's min channel is U8)
		if (png_get_valid(png, info, PNG_INFO_tRNS) != 0)
			png_set_tRNS_to_alpha(png); // colourkey -> a real alpha channel
		if (depth == 16 && hostIsLittleEndian())
			png_set_swap(png); // big-endian samples -> native u16
		png_read_update_info(png, info);

		depth = png_get_bit_depth(png, info);
		const int channels = png_get_channels(png, info);
		const png_size_t rowBytes = png_get_rowbytes(png, info);

		const lain::image::PixelFormat format = pixelFormatFor(channels, depth);
		const bool hasAlpha = channels == 2 || channels == 4;
		const auto alphaMode = hasAlpha ? lain::image::AlphaMode::Straight : lain::image::AlphaMode::Unspecified;

		// Size the destination and read directly into its rows (tightly packed, so the PNG
		// row stride equals the Image's). Assigning `out` is safe here — see the note above.
		out = lain::image::Image(static_cast<int>(w), static_cast<int>(h), format, colorSpace, alphaMode);

		rows = static_cast<png_bytep*>(std::malloc(sizeof(png_bytep) * h));
		if (rows == nullptr)
			png_error(png, "out of memory"); // longjmps to the cleanup above
		for (png_uint_32 y = 0; y < h; ++y)
			rows[y] = out.data() + static_cast<std::size_t>(y) * rowBytes;

		png_read_image(png, rows);
		png_read_end(png, nullptr);

		std::free(rows);
		png_destroy_read_struct(&png, &info, nullptr);
		return true;
	}

	// The PNG reader: libpng in, a CPU image::Image out. Stateless; the registry makes one
	// per decode.
	//
	// Format policy (documented lossless expansion — the reader's contract, not a silent
	// surprise): formats lain represents natively are PRESERVED — RGB/RGBA and grayscale /
	// gray+alpha at 8 or 16 bit map straight to Gray8/16, GrayAlpha8/16, RGB8/16, RGBA8/16.
	// Formats lain can't hold are expanded losslessly in pixel value: palette -> RGB(A),
	// sub-8-bit -> 8-bit, tRNS colourkey -> a real alpha channel. ColorSpace comes from the
	// file's own chunks (Unspecified when untagged) — see colorSpaceFromPng. Still on the
	// "expand" list because lain can't yet represent them: indexed/palette, sub-byte depths,
	// colourkey (vs. full alpha), and non-sRGB/ICC colour spaces (WORK.md M3).
	class PngReader : public ImageReader
	{
	public:
		lain::image::Image decode(const memory::Buffer& bytes) const override
		{
			lain::image::Image image;
			if (!decodePngInto(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), image))
				return {}; // invalid Image -> the facade turns this into a logged nullopt
			return image;
		}
	};

	void registerPngReader()
	{
		readerRegistry().registerType<PngReader>("png");
	}
} // namespace lain::io::image::png
