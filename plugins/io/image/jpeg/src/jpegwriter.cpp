#include "jpegwriter.h"

#include "lain/io/image/save.h" // ImageWriter, writerRegistry
#include "lain/io/image/writer.h"

#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/memory/buffer.h>

#include <stb_image_write.h> // declarations only; the impl is compiled in addStb's TU

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace lain::io::image::jpeg
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// stb_image_write's callback: append the encoded bytes to the std::vector passed as the
	// context. A growable vector accumulates the stream; encode() copies it once into a fixed
	// memory::Buffer (Buffer is non-resizable by design — same as the png writer).
	static void writeToVector(void* context, void* data, int size)
	{
		auto* out = static_cast<std::vector<std::uint8_t>*>(context);
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		out->insert(out->end(), bytes, bytes + static_cast<std::size_t>(size));
	}

	// The JPEG writer: a CPU image::Image in, JPEG bytes out via stb_image_write. Stateless.
	//
	// JPEG is 8-bit with no alpha, so only Gray8 and RGB8 encode without loss — everything else
	// (alpha-bearing, 16-bit, float) is rejected by canEncode(), never silently converted. To
	// save an RGBA image as JPEG the caller drops alpha itself (convert to RGB8) — an explicit
	// choice, not a hidden one. Quality uses a fixed default; a quality knob is deferred
	// (WORK.md M3: encoder config, decided with all three encoder surfaces in view).
	class JpegWriter : public ImageWriter
	{
	public:
		bool canEncode(const lain::image::Image& image) const override
		{
			if (!image.valid())
				return false;
			const auto desc = image.descriptor();
			if (desc.channelType != lain::image::ChannelType::U8)
				return false; // JPEG is 8-bit
			return desc.model == lain::image::ColorModel::Gray || desc.model == lain::image::ColorModel::RGB;
		}

		std::optional<memory::Buffer> encode(const lain::image::Image& image) const override
		{
			const auto desc = image.descriptor();
			int components = 0;
			if (desc.channelType == lain::image::ChannelType::U8 && desc.model == lain::image::ColorModel::Gray)
				components = 1;
			else if (desc.channelType == lain::image::ChannelType::U8 && desc.model == lain::image::ColorModel::RGB)
				components = 3;
			else
				return std::nullopt; // defensive: the seam's canEncode should have rejected this

			constexpr int kQuality = 90; // deferred config knob
			std::vector<std::uint8_t> bytes;
			if (stbi_write_jpg_to_func(&writeToVector, &bytes, image.width(), image.height(), components, image.data(),
									   kQuality) == 0)
				return std::nullopt;

			memory::Buffer buffer(bytes.size());
			std::memcpy(buffer.data(), bytes.data(), bytes.size());
			return buffer;
		}
	};

	void registerJpegWriter()
	{
		writerRegistry().registerType<JpegWriter>("jpg");
		writerRegistry().registerType<JpegWriter>("jpeg");
	}
} // namespace lain::io::image::jpeg
