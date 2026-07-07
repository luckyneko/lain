#pragma once

#include "lain/image/color.h" // Color + typedefs

// The color-value algorithms: free functions over Color (luminance / saturate / convert).
// Kept apart from the Color *type* (color.h) so a consumer that only needs the type doesn't
// pull the math — the same types-vs-algorithms split as imageview.h / traverse.h.
namespace lain::image
{
	// The Rec709 relative luminance of a color's RGB, in unit [0,1] space (Gray replicates
	// its single channel). The weights only mean "brightness" in linear light — callers
	// that care enforce ColorSpace::Linear (see image::convert / the op layer).
	template <PixelFormat F>
	float luminance(const Color<F>& c);

	// Clamp every channel into its valid range ([0,1] for float, [0,max] for integral).
	template <PixelFormat F>
	Color<F> saturate(const Color<F>& c);

	// Convert a color to another Color type: normalizes channel values across base types
	// (through a unit-[0,1] intermediate) and maps channel counts through a canonical unit
	// RGBA (Gray replicates; RGB/RGBA gain opaque alpha; a Gray target takes Rec709
	// luminance). The mechanical core of image::convert(); color-space/alpha enforcement
	// lives at the image level, not here.
	template <typename Dst, PixelFormat Src>
	Dst convert(const Color<Src>& src);

	// Apply a unit-[0,1] function to each pixel's color channels of a view — every channel
	// but a trailing alpha (which is linear and left as-is). The per-pixel-channel workhorse
	// the transfer / tone ops are built on (fn: float -> float in unit space).
	template <typename View, typename Fn>
	void mapColorChannels(View view, Fn fn);
} // namespace lain::image

#include "lain/image/details/colormath.inl"
