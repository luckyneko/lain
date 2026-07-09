#pragma once

#include "lain/io/image/writer.h"

#include <lain/core/factory.h>
#include <lain/image/image.h>
#include <lain/memory/buffer.h>

#include <optional>
#include <string_view>

namespace lain::io::image
{
	// The writer registry — the process-wide table of ImageWriters keyed by a lowercase format
	// key ("png" / "tiff" / "jpg"). A codec plugin registers its writer here alongside its
	// reader (registerCodec registers both), and save()/encode() look one up by key. The write
	// counterpart of readerRegistry().
	lain::core::Factory<ImageWriter>& writerRegistry();

	// Whether the writer for `formatKey` can encode `image` WITHOUT loss (false if no writer is
	// registered for the key, or the writer would have to degrade the image — e.g. JPEG can't
	// hold alpha or 16-bit). A quiet predicate (no logging): the honest pre-check a caller runs
	// before offering "Save as <format>", so it can guide a convert instead of hitting encode's
	// loud rejection. encode()/save() enforce the same check.
	[[nodiscard]] bool canEncode(std::string_view formatKey, const lain::image::Image& image);

	// Encode `image` to `formatKey` bytes in memory. std::nullopt if no writer is registered
	// for the key or the writer fails (reason logged). For bytes you want in a Buffer rather
	// than a file; save() is the to-uri path.
	[[nodiscard]] std::optional<memory::Buffer> encode(std::string_view formatKey, const lain::image::Image& image);

	// Encode `image` (format chosen by the uri's extension) and write it to `uri` (io::write).
	// Returns false on an invalid image, a missing/unknown extension, an encode failure, or a
	// write failure — the reason is logged. The single "Image + path in, file out" entry point.
	[[nodiscard]] bool save(std::string_view uri, const lain::image::Image& image);
} // namespace lain::io::image
