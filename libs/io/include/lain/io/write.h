#pragma once

#include <lain/memory/buffer.h>

#include <string_view>

namespace lain::io
{
	// Write `bytes` to the resource named by `uri` — the transport write seam, sibling of
	// read(). Like read, `uri` is a scheme-prefixed locator; a bare path or file:// writes a
	// local file (created, or truncated if it exists), and only that scheme is served today —
	// remote/s3 dispatch behind this same function later. io moves bytes and never encodes:
	// turning an Image into format bytes is a codec's job (lain::io::image).
	//
	// Returns false when the write can't be done — an unsupported scheme, or an open / write
	// error (e.g. a missing parent directory). The reason is logged; the caller decides how to
	// surface it. An empty Buffer writes a zero-length file.
	[[nodiscard]] bool write(std::string_view uri, const memory::Buffer& bytes);
} // namespace lain::io
