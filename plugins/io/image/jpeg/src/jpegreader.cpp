#include "jpegreader.h"

#include "jpegcolor.h" // the APP-marker scan — stb decodes pixels and discards every marker

#include <lain/image/image.h>
#include <lain/io/image/load.h>	  // readerRegistry
#include <lain/io/image/reader.h> // ImageReader
#include <lain/log/log.h>
#include <lain/memory/buffer.h>

#include <stb_image.h> // declarations only; the implementation is compiled in addStb's TU

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lain::io::image::jpeg
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// stb decodes JPEG to 8-bit at the file's native channel count: 1 (grayscale) or 3
	// (RGB — YCbCr is converted for us). 2/4 don't occur for JPEG but are mapped for
	// completeness.
	static lain::image::PixelFormat pixelFormatFor(int channels)
	{
		switch (channels)
		{
			case 1:
				return lain::image::PixelFormat::Gray8;
			case 2:
				return lain::image::PixelFormat::GrayAlpha8;
			case 3:
				return lain::image::PixelFormat::RGB8;
			default:
				return lain::image::PixelFormat::RGBA8;
		}
	}

	// The JPEG reader: stb_image in, a CPU image::Image out. Stateless; the registry makes
	// one per decode. JPEG is 8-bit with no alpha, so the result is Gray8 or RGB8; its
	// ColorSpace comes from the file's own APP markers (see jpegcolor.h), NOT from the format
	// — a JPEG carrying an ICC profile or an explicitly uncalibrated Exif tag is no longer
	// claimed as sRGB. stb owns its decode allocation, so unlike the png/tiff
	// readers there is one copy from stb's buffer into the Image (stb offers no decode-into
	// API). Speed/quality is below libjpeg-turbo but adequate; a turbojpeg swap can replace
	// this behind the same seam later (WORK.md M3).
	class JpegReader : public ImageReader
	{
	public:
		lain::image::Image decode(const memory::Buffer& bytes) const override
		{
			if (bytes.size() > static_cast<std::size_t>(INT_MAX))
				return {}; // stb takes an int length

			// Read the markers BEFORE decoding: the scan wants the whole encoded buffer, and
			// there is no reason to decode megapixels for a file whose header is malformed.
			const auto* encoded = reinterpret_cast<const std::uint8_t*>(bytes.data());
			const lain::image::ColorSpace colorSpace = colorSpaceFromMarkers(encoded, bytes.size());
			if (colorSpaceIsJfifConvention(encoded, bytes.size()))
			{
				// Said out loud rather than assumed quietly (ADR-0020): JFIF fixes no transfer, so
				// this arm is convention, not a statement the file made. At debug rather than info
				// because a stills sequence decodes one file per frame.
				lain::log::debug("io::image jpeg: no colour marker beyond JFIF — treating it as sRGB");
			}

			int width = 0;
			int height = 0;
			int channels = 0;
			stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
													static_cast<int>(bytes.size()), &width, &height, &channels, 0);
			if (pixels == nullptr)
				return {}; // invalid Image -> the facade turns this into a logged nullopt

			lain::image::Image image(width, height, pixelFormatFor(channels), colorSpace,
									 lain::image::AlphaMode::Unspecified);
			std::memcpy(image.data(), pixels, image.byteSize());
			stbi_image_free(pixels);
			return image;
		}
	};

	void registerJpegReader()
	{
		readerRegistry().registerType<JpegReader>("jpg");
		readerRegistry().registerType<JpegReader>("jpeg");
	}
} // namespace lain::io::image::jpeg
