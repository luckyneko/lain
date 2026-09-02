#pragma once

#include "lain/image/colorspace.h" // ColorSpace, AlphaMode (conversion targets)
#include "lain/image/image.h"
#include "lain/image/pixelformat.h" // PixelFormat (conversion target)

// Image-level conversions — the explicit, user-driven transforms that also maintain the
// tracked ColorSpace / AlphaMode tags. One verb, `convert`, overloaded on what you convert
// *to*: a PixelFormat, a ColorSpace, or an AlphaMode. Each is target-based and direction-
// agnostic (converting to the value the image is already in is a no-op). Nothing converts
// implicitly — a space/alpha-sensitive op elsewhere enforces its tag rather than calling
// these. Each returns a new Image; the source is untouched. A precondition violation logs
// and returns an invalid Image (asserts in debug, via log::ensure).
namespace lain::image
{
	// Convert to another PixelFormat (channel layout + base type). Base-type changes
	// normalize through unit [0,1]; channel-count changes add opaque alpha (RGB->RGBA), drop
	// it (RGBA->RGB), or replicate (Gray->RGB). A reduction to Gray uses Rec709 luminance, so
	// it REQUIRES ColorSpace::Linear. The color-space tag carries through; alpha mode carries
	// through, and a newly-added (opaque) alpha channel is Straight.
	Image convert(const Image& src, PixelFormat dstFormat);

	// Convert to a ColorSpace: applies the transfer between the source space and `dstSpace` on
	// the color channels only (alpha is linear and untouched; Gray's single channel is treated
	// as color). REQUIRES a known (non-Unspecified) source space. Conversion COMPOSES THROUGH
	// LINEAR LIGHT, so every pair of known spaces is expressible and a single call is always
	// one pass — chaining two calls to reach a third space quantises twice, this does not.
	//
	// Known limitation: this reads only the space tag, not alphaMode(). Applying a nonlinear
	// curve to Premultiplied color is wrong, and nothing here catches it — convert the SPACE
	// first and the alpha mode second (as flow-example's BlurNode does).
	Image convert(const Image& src, ColorSpace dstSpace);

	// Convert to an AlphaMode: premultiplies / un-premultiplies the color channels by alpha
	// (alpha-bearing formats; a no-op on formats without alpha). REQUIRES a known
	// (non-Unspecified) source alpha mode.
	Image convert(const Image& src, AlphaMode dstAlpha);
} // namespace lain::image
