#pragma once

#include <lain/data/value.h>
#include <lain/memory/buffer.h>

#include <optional>

namespace lain::io::data
{
	// A DataWriter encodes a lain::data::Value into a memory::Buffer of one format's bytes — the
	// encode peer of DataReader. One implementation per format (JsonWriter, …); stateless, made
	// once per encode by the writer registry.
	//
	// Like DataReader, this interface names no third-party parser. A text writer encodes a Bytes
	// node however that format carries binary (Base64 for JSON) — the "Base64 is a per-format
	// encoding" rule; the DOM never holds Base64.
	class DataWriter
	{
	public:
		virtual ~DataWriter() = default;

		// Encode `value` to format bytes, or std::nullopt on an encode error (e.g. a value the
		// format can't represent — a NaN/Inf double in JSON). The save() facade logs a nullopt.
		virtual std::optional<memory::Buffer> encode(const lain::data::Value& value) const = 0;
	};
} // namespace lain::io::data
