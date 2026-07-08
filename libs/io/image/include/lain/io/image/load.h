#pragma once

#include "lain/io/image/reader.h"

#include <lain/core/factory.h>
#include <lain/image/image.h>
#include <lain/memory/buffer.h>

#include <optional>
#include <string_view>

namespace lain::io::image
{
	// The reader registry — the process-wide table of ImageReaders keyed by a lowercase
	// format key (the file extension without the dot: "jpg" / "png" / "tiff"). A codec
	// plugin registers its reader here — readerRegistry().registerType<JpegReader>("jpg")
	// — and load()/decode() look one up by key. Exposed as the single mutation seam so a
	// codec (static today, a thorax plugin later) attaches without lain::io::image naming
	// it; there is no other named global.
	lain::core::Factory<ImageReader>& readerRegistry();

	// Decode in-memory `bytes` already known to be `formatKey` (lowercase, no dot).
	// Returns std::nullopt when no reader is registered for the key or the reader rejects
	// the bytes (reason logged). For bytes that did not come from a file (e.g. an
	// embedded resource); load() is the from-uri path.
	[[nodiscard]] std::optional<lain::image::Image> decode(std::string_view formatKey, const memory::Buffer& bytes);

	// Load and decode the image at `uri`: read its bytes (io::read), pick the reader by
	// the uri's file extension, decode. Returns std::nullopt on a read failure, a missing
	// extension / unknown format, or a decode failure — the reason is logged in each
	// case. The single "file path in, Image out" entry point.
	[[nodiscard]] std::optional<lain::image::Image> load(std::string_view uri);
} // namespace lain::io::image
