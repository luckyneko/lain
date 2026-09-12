#pragma once

#include "lain/io/stream.h"

#include <lain/core/uri.h>
#include <lain/memory/buffer.h>

#include <memory>
#include <optional>

namespace lain::io
{
	// The transport ENTRY POINTS — every way into the bytes a uri names, in one header. This is the
	// door a caller knocks on; stream.h is the contract a backend implements behind it. That is the
	// split every medium seam under io already runs — load.h/save.h are the entry points,
	// reader.h/writer.h the interface — and this file is io's own top level finally saying it too.
	//
	// THE INCLUDE RUNS ONE WAY: transport.h includes stream.h and never the reverse. That is what
	// lets a file which only implements or holds a Stream include stream.h alone and know nothing
	// about whole-asset reads — io::video's reader.h/writer.h and the FFmpeg plugin's AVIO bridge
	// are all of them. A stream.h that reached back here would hand every codec plugin an API it
	// has no business having.
	//
	// TWO SHAPES OVER ONE BACKEND PER SCHEME. read/write are the whole-asset use of a stream (open,
	// size, fill / create, push, finish), so "get the bytes at this uri" has exactly one
	// implementation per scheme rather than two that can drift. Which shape a caller wants is a
	// question about the asset: a json document is read whole, a video container is opened once and
	// seeked around for as long as any evaluation holds it (ADR-0018).
	//
	// `uri` is a core::Uri throughout — the name of one resource, opaquely split as "scheme://rest"
	// (ADR-0023). A bare path with no scheme is the local scheme, and only local/file is served
	// today; remote/s3 schemes dispatch behind these same four functions later, with no change to
	// callers. Every failure is logged with its reason and reported by the return value, so a
	// caller decides how to surface it and never has to log twice.
	//
	// io MOVES BYTES AND NEVER DECODES. Turning a Buffer into a typed asset is a codec's job
	// (lain::io::image, ::data, ::video), which is what keeps this library codec-dependency-free.

	// Read the whole resource named by `uri` into a memory::Buffer.
	//
	// Returns the resource's bytes (a valid, possibly-empty Buffer for a zero-length file), or
	// std::nullopt when it cannot be read — missing file, open/read error, or an unsupported
	// scheme. The returned Buffer is exactly the resource size, cache-line aligned.
	[[nodiscard]] std::optional<memory::Buffer> read(const core::Uri& uri);

	// Write `bytes` to the resource named by `uri`, which is created, or truncated if it exists.
	//
	// Returns false when the write cannot be done — an unsupported scheme, or an open / write error
	// (e.g. a missing parent directory). An empty Buffer writes a zero-length file.
	[[nodiscard]] bool write(const core::Uri& uri, const memory::Buffer& bytes);

	// Open `uri` for incremental reading, or nullptr when it cannot be opened — missing file, not a
	// regular file, or an unsupported scheme.
	[[nodiscard]] std::unique_ptr<ReadStream> openStream(const core::Uri& uri);

	// Create (or truncate) `uri` for incremental writing, or nullptr when it cannot be created — a
	// missing parent directory, no permission, or an unsupported scheme. Named create rather than
	// open because it is destructive, which is what write() already does to an existing file.
	[[nodiscard]] std::unique_ptr<WriteStream> createStream(const core::Uri& uri);
} // namespace lain::io
