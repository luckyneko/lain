#pragma once

#include <lain/image/image.h>
#include <lain/memory/buffer.h>

#include <optional>

namespace lain::io::image
{
	// An ImageWriter encodes a CPU lain::image::Image into a memory::Buffer of one format's
	// bytes — the encode counterpart of ImageReader. One implementation per format
	// (PngWriter, TiffWriter, …); registered into the writer registry by a lowercase format
	// key. Stateless; the registry makes one per encode.
	//
	// Like ImageReader, this interface names no third-party codec. Encoder config (jpeg
	// quality, png compression level, tiff compression) is deferred — its shape is decided
	// once the concrete encoders land and their real needs are visible (WORK.md M3), so the
	// v1 interface takes just the image.
	class ImageWriter
	{
	public:
		virtual ~ImageWriter() = default;

		// Encode `image` to format bytes, or std::nullopt on failure (an input the format
		// can't represent, or an encode error). The save() facade logs a nullopt result.
		virtual std::optional<memory::Buffer> encode(const lain::image::Image& image) const = 0;
	};
} // namespace lain::io::image
