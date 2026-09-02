#include "lain/io/image/load.h"

#include <lain/io/read.h>
#include <lain/io/uri.h>
#include <lain/log/log.h>

#include <string>

namespace lain::io::image
{
	lain::core::Factory<ImageReader>& readerRegistry()
	{
		static lain::core::Factory<ImageReader> registry;
		return registry;
	}

	std::optional<lain::image::Image> decode(std::string_view formatKey, const memory::Buffer& bytes)
	{
		const std::string key(formatKey);
		const auto reader = readerRegistry().create(key);
		if (!reader)
		{
			log::warn("io::image::decode: no reader registered for format '{}'", key);
			return std::nullopt;
		}

		lain::image::Image image = reader->decode(bytes);
		if (!image.valid())
		{
			log::error("io::image::decode: reader for '{}' rejected {} bytes", key, bytes.size());
			return std::nullopt;
		}
		return image;
	}

	std::optional<lain::image::Image> load(std::string_view uri)
	{
		const std::string key = lain::io::extensionKey(uri);
		if (key.empty())
		{
			log::warn("io::image::load: no file extension to select a reader: {}", std::string(uri));
			return std::nullopt;
		}

		const auto bytes = io::read(uri);
		if (!bytes)
			return std::nullopt; // io::read logged the reason already

		return decode(key, *bytes);
	}
} // namespace lain::io::image
