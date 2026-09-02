#include "lain/io/data/load.h"

#include <lain/io/read.h>
#include <lain/io/uri.h>
#include <lain/log/log.h>

#include <string>

namespace lain::io::data
{
	lain::core::Factory<DataReader>& readerRegistry()
	{
		static lain::core::Factory<DataReader> registry;
		return registry;
	}

	std::optional<lain::data::Value> decode(std::string_view formatKey, const memory::Buffer& bytes)
	{
		const std::string key(formatKey);
		const auto reader = readerRegistry().create(key);
		if (!reader)
		{
			log::warn("io::data::decode: no reader registered for format '{}'", key);
			return std::nullopt;
		}

		auto value = reader->decode(bytes);
		if (!value)
		{
			log::error("io::data::decode: reader for '{}' rejected {} bytes", key, bytes.size());
			return std::nullopt;
		}
		return value;
	}

	std::optional<lain::data::Value> load(std::string_view uri)
	{
		const std::string key = lain::io::extensionKey(uri);
		if (key.empty())
		{
			log::warn("io::data::load: no file extension to select a reader: {}", std::string(uri));
			return std::nullopt;
		}

		const auto bytes = io::read(uri);
		if (!bytes)
			return std::nullopt; // io::read logged the reason already

		return decode(key, *bytes);
	}
} // namespace lain::io::data
