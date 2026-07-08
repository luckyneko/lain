#pragma once

#include <lain/memory/buffer.h>

#include <optional>
#include <string_view>

namespace lain::io
{
	// Read the whole resource named by `uri` into a memory::Buffer — the transport
	// front seam. This is the one entry point every file/asset load goes through, so
	// nothing else in the set scatters raw fstream/filesystem calls.
	//
	// `uri` is a scheme-prefixed locator ("scheme://rest"); a bare path with no scheme
	// is treated as a local file. Only the `local`/`file` scheme is served today —
	// remote/s3 schemes dispatch behind this same function later, with no change to
	// callers. io moves bytes and never decodes: turning a Buffer into a typed asset is
	// a codec's job (lain::io::image).
	//
	// Returns the file's bytes (a valid, possibly-empty Buffer for a zero-length file),
	// or std::nullopt when the resource can't be read — missing file, open/read error,
	// or an unsupported scheme. The reason is logged; the caller decides how to surface
	// it. The returned Buffer is exactly the resource size, cache-line aligned.
	[[nodiscard]] std::optional<memory::Buffer> read(std::string_view uri);
} // namespace lain::io
