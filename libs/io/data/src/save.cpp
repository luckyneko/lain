#include "lain/io/data/save.h"

#include "formatkey.h" // formatKeyFromUri (shared with load.cpp)

#include <lain/io/write.h>
#include <lain/log/log.h>

#include <string>

namespace lain::io::data
{
	lain::core::Factory<DataWriter>& writerRegistry()
	{
		static lain::core::Factory<DataWriter> registry;
		return registry;
	}

	std::optional<memory::Buffer> encode(std::string_view formatKey, const lain::data::Value& value)
	{
		const std::string key(formatKey);
		const auto writer = writerRegistry().create(key);
		if (!writer)
		{
			log::warn("io::data::encode: no writer registered for format '{}'", key);
			return std::nullopt;
		}

		auto bytes = writer->encode(value);
		if (!bytes)
		{
			log::error("io::data::encode: writer for '{}' failed to encode", key);
			return std::nullopt;
		}
		return bytes;
	}

	bool save(std::string_view uri, const lain::data::Value& value)
	{
		const std::string key = formatKeyFromUri(uri);
		if (key.empty())
		{
			log::warn("io::data::save: no file extension to select a writer: {}", std::string(uri));
			return false;
		}

		const auto bytes = encode(key, value);
		if (!bytes)
			return false; // encode logged the reason

		return io::write(uri, *bytes);
	}
} // namespace lain::io::data
