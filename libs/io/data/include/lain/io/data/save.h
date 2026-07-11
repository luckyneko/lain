#pragma once

#include "lain/io/data/writer.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/memory/buffer.h>

#include <optional>
#include <string_view>

namespace lain::io::data
{
	// The writer registry — the process-wide table of DataWriters keyed by a lowercase format key
	// ("json" / "yaml" / "xml"). A codec plugin registers its writer here alongside its reader
	// (registerCodec registers both), and save()/encode() look one up by key. The write counterpart
	// of readerRegistry().
	lain::core::Factory<DataWriter>& writerRegistry();

	// Encode `value` to `formatKey` bytes in memory. std::nullopt if no writer is registered for
	// the key or the writer fails (reason logged). For bytes you want in a Buffer (a websocket
	// frame) rather than a file; save() is the to-uri path.
	[[nodiscard]] std::optional<memory::Buffer> encode(std::string_view formatKey, const lain::data::Value& value);

	// Encode `value` (format chosen by the uri's extension) and write it to `uri` (io::write).
	// Returns false on a missing/unknown extension, an encode failure, or a write failure — the
	// reason is logged. The single "Value + path in, file out" entry point.
	[[nodiscard]] bool save(std::string_view uri, const lain::data::Value& value);
} // namespace lain::io::data
