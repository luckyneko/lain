#pragma once

// A synthetic FrameSource for the media tests — driver-free, file-free, and instrumented.
//
// The base class owns memoisation, range checking and spec enforcement, so those are what the
// tests need to exercise; a real medium's decoder is exactly what they must NOT depend on. This
// source counts its decodes (so a cache hit is observable rather than assumed) and can be told
// to fail or to produce an off-spec frame at a chosen ordinal.

#include <lain/image/image.h>
#include <lain/media/framesource.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace lain::media::test
{
	inline constexpr std::size_t never = std::numeric_limits<std::size_t>::max();

	// The spec these sources declare, and the shape TestSource produces.
	inline FrameSpec testSpec(FrameRate rate = FrameRate{25, 1})
	{
		FrameSpec spec;
		spec.extent = {4, 2};
		spec.pixelFormat = lain::image::PixelFormat::RGBA8;
		spec.colorSpace = lain::image::ColorSpace::sRGB;
		spec.alphaMode = lain::image::AlphaMode::Straight;
		spec.rate = rate;
		return spec;
	}

	class TestSource : public FrameSource
	{
	public:
		TestSource(std::string uri, std::size_t frameCount, FrameSpec spec = testSpec(),
				   std::size_t cacheFrames = FrameSource::defaultCacheFrames)
			: FrameSource{std::move(uri), spec, frameCount, cacheFrames}
		{
		}

		// How many times decodeFrame actually ran — the only way to tell a cache hit from a
		// re-decode, since both return an equal image.
		mutable int decodeCount = 0;

		std::size_t failAt = never;		// this ordinal fails to decode
		std::size_t mismatchAt = never; // this ordinal decodes at the wrong extent

		// Every frame is a solid image whose first byte is its ordinal, so a test can name which
		// frame it got back without decoding anything itself.
		static std::uint8_t tagOf(const lain::image::Image& image)
		{
			return image.valid() ? image.data()[0] : 0;
		}

	protected:
		lain::image::Image decodeFrame(std::size_t ordinal) const override
		{
			++decodeCount;
			if (ordinal == failAt)
				return {};

			const FrameSpec& declared = spec();
			const int width = ordinal == mismatchAt ? declared.extent.x + 1 : declared.extent.x;
			lain::image::Image image{width, declared.extent.y, declared.pixelFormat, declared.colorSpace,
									 declared.alphaMode};
			image.data()[0] = static_cast<std::uint8_t>(ordinal);
			return image;
		}
	};
} // namespace lain::media::test
