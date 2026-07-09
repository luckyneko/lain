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

		// Whether this writer can encode `image` WITHOUT LOSS — the format natively represents
		// its pixel format, channel count, and bit depth. A codec must NOT silently degrade an
		// input it can't hold (JPEG has no alpha; PNG has no float): it reports false here, and
		// the seam rejects loudly, leaving any lossy conversion (e.g. dropping alpha) to the
		// caller's explicit convert(). The precondition for encode().
		virtual bool canEncode(const lain::image::Image& image) const = 0;

		// Encode `image` to format bytes, or std::nullopt on an encode error. Precondition:
		// canEncode(image) is true (the seam checks it first). The save() facade logs a nullopt.
		virtual std::optional<memory::Buffer> encode(const lain::image::Image& image) const = 0;
	};
} // namespace lain::io::image
