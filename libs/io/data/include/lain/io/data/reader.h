#pragma once

#include <lain/data/value.h>
#include <lain/memory/buffer.h>

#include <optional>

namespace lain::io::data
{
	// A DataReader decodes the bytes of one serialization format — a memory::Buffer holding a
	// whole document — into a lain::data::Value (the format-neutral DOM). One implementation per
	// format (JsonReader, a future YamlReader/XmlReader); its matching DataWriter is its encode
	// peer. Readers are stateless: the reader registry constructs one per decode.
	//
	// This interface names no third-party parser. Concrete readers live in codec plugins
	// (plugins/io/data/<fmt>) that depend on this seam and register into the reader registry — so
	// lain::io::data itself pulls in no nlohmann/ryml/pugixml.
	class DataReader
	{
	public:
		virtual ~DataReader() = default;

		// Decode `bytes` into a Value. Return std::nullopt on malformed input; the load()/decode()
		// facade turns that into a logged failure, so a reader need not log or throw itself. (A
		// decoded Null is a valid Value, not a failure — hence optional, not a Null sentinel.)
		virtual std::optional<lain::data::Value> decode(const memory::Buffer& bytes) const = 0;
	};
} // namespace lain::io::data
