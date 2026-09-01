#pragma once

// The local-filesystem Stream backends (private to the library — not a public header,
// exactly like scheme.h). Callers reach these through io::openStream / io::createStream;
// nothing outside lain::io names a concrete stream type, which is what keeps the promise
// that a Stream never hands out an OS handle honest at the type level.

#include "lain/io/stream.h"

#include <filesystem>
#include <memory>
#include <string>

namespace lain::io
{
	// Open `path` for reading, or nullptr (reason logged) if it is not a readable regular
	// file. `uri` is the canonical name the stream reports and reopens by.
	[[nodiscard]] std::unique_ptr<ReadStream> openLocalReadStream(const std::filesystem::path& path, std::string uri);

	// Create or truncate `path` for writing, or nullptr (reason logged) if it cannot be
	// opened — a missing parent directory, no permission.
	[[nodiscard]] std::unique_ptr<WriteStream> createLocalWriteStream(const std::filesystem::path& path, std::string uri);
} // namespace lain::io
