#include "lain/io/image/save.h"

#include "formatkey.h" // formatKeyFromUri (shared with load.cpp)

#include <lain/io/write.h>
#include <lain/log/log.h>

#include <string>

namespace lain::io::image
{
	lain::core::Factory<ImageWriter>& writerRegistry()
	{
		static lain::core::Factory<ImageWriter> registry;
		return registry;
	}

	std::optional<memory::Buffer> encode(std::string_view formatKey, const lain::image::Image& image)
	{
		const std::string key(formatKey);
		const auto writer = writerRegistry().create(key);
		if (!writer)
		{
			log::warn("io::image::encode: no writer registered for format '{}'", key);
			return std::nullopt;
		}

		if (!writer->canEncode(image))
		{
			log::warn("io::image::encode: the '{}' codec cannot encode {} without loss; convert it first", key,
					  image.toString());
			return std::nullopt;
		}

		auto bytes = writer->encode(image);
		if (!bytes)
		{
			log::error("io::image::encode: writer for '{}' failed to encode {}", key, image.toString());
			return std::nullopt;
		}
		return bytes;
	}

	bool save(std::string_view uri, const lain::image::Image& image)
	{
		if (!image.valid())
		{
			log::warn("io::image::save: refusing to save an invalid image to: {}", std::string(uri));
			return false;
		}

		const std::string key = formatKeyFromUri(uri);
		if (key.empty())
		{
			log::warn("io::image::save: no file extension to select a writer: {}", std::string(uri));
			return false;
		}

		auto bytes = encode(key, image);
		if (!bytes)
			return false; // encode logged the reason already

		return io::write(uri, *bytes);
	}
} // namespace lain::io::image
