#pragma once

#include <lain/image/image.h>
#include <lain/memory/buffer.h>

namespace lain::io::image
{
	// A Reader decodes the encoded bytes of one image format — a memory::Buffer holding
	// a whole file's contents — into a CPU lain::image::Image. One Reader implementation
	// per format (JpegReader, PngReader, …); a matching Writer (encode) is a deferred
	// peer, added when a save path has a real caller. Readers are expected to be
	// stateless: the reader registry constructs one per decode.
	//
	// This interface names no third-party codec. Concrete readers live in codec plugins
	// (plugins/io/image/<fmt>) that depend on this seam and register into the reader
	// registry — so lain::io::image itself pulls in no libjpeg/png/tiff (WORK.md M3).
	class ImageReader
	{
	public:
		virtual ~ImageReader() = default;

		// Decode `bytes` into an Image. Return an invalid Image (image::Image::valid()
		// == false) on malformed input; the load()/decode() facade turns that into a
		// logged std::nullopt, so a reader never has to log or throw itself.
		virtual lain::image::Image decode(const memory::Buffer& bytes) const = 0;
	};
} // namespace lain::io::image
