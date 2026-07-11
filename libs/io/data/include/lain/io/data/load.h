#pragma once

#include "lain/io/data/reader.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/memory/buffer.h>

#include <optional>
#include <string_view>

namespace lain::io::data
{
	// The reader registry — the process-wide table of DataReaders keyed by a lowercase format key
	// (the file extension without the dot: "json" / "yaml" / "xml"). A codec plugin registers its
	// reader here — readerRegistry().registerType<JsonReader>("json") — and load()/decode() look
	// one up by key. The single mutation seam, so a codec attaches without io::data naming it.
	lain::core::Factory<DataReader>& readerRegistry();

	// Decode in-memory `bytes` already known to be `formatKey` (lowercase, no dot). std::nullopt
	// when no reader is registered for the key or the reader rejects the bytes (reason logged).
	// For bytes that did not come from a file (e.g. a websocket frame); load() is the from-uri path.
	[[nodiscard]] std::optional<lain::data::Value> decode(std::string_view formatKey, const memory::Buffer& bytes);

	// Load and decode the document at `uri`: read its bytes (io::read), pick the reader by the
	// uri's file extension, decode. std::nullopt on a read failure, a missing extension / unknown
	// format, or a decode failure — the reason is logged. The "file path in, Value out" entry point.
	[[nodiscard]] std::optional<lain::data::Value> load(std::string_view uri);
} // namespace lain::io::data
