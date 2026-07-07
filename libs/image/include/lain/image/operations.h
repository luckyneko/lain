#pragma once

#include "lain/image/image.h"

#include <lain/math/types.h> // math::Vec2i (resize extent)

#include <cstddef> // std::size_t (Kernel::at)
#include <vector>  // Kernel weights

// The image operation catalog — free functions over Image, each returning a new Image.
// Space/alpha enforcement follows the op class (decision #8): value-blending / cross-pixel
// ops (convolve, sharpen, resize, arbitrary rotate) require ColorSpace::Linear and, for
// alpha-bearing formats, AlphaMode::Premultiplied; nonlinear per-pixel tone ops (contrast,
// gamma) require AlphaMode::Straight; pixel-rearranging ops (clamp, brightness, crop,
// rotate90) are agnostic. A precondition violation logs and returns an invalid Image (via
// log::ensure — asserts in debug). Grouped by section below.
namespace lain::image
{
	// --- interpolation & kernels -------------------------------------------------

	// Resampling method for value-blending geometry (resize / rotate). Nearest + Bilinear
	// today; Bicubic / Lanczos + an area downscale filter arrive later.
	enum class Interpolation
	{
		Nearest,
		Bilinear,
	};

	// A square convolution kernel of odd side 2*radius+1, weights stored row-major. Weights
	// are applied as-is (a blur kernel should sum to 1; build one with gaussianKernel).
	struct Kernel
	{
		int radius = 0;
		std::vector<float> weights;

		int size() const { return 2 * radius + 1; }
		// Weight at kernel offset (kx, ky), each in [-radius, radius].
		float at(int kx, int ky) const { return weights[static_cast<std::size_t>((ky + radius) * size() + (kx + radius))]; }
	};

	// A normalized Gaussian kernel (weights sum to 1) of the given radius and sigma.
	Kernel gaussianKernel(int radius, float sigma);

	// --- per-pixel tone ----------------------------------------------------------

	// Multiply the color channels by `factor` (alpha untouched) — a linear per-pixel scale,
	// space- and alpha-agnostic. Integer channels clamp at their max; float may exceed 1.
	Image brightness(const Image& src, float factor);

	// Scale the color channels around mid-gray 0.5 by `factor`. Nonlinear per-pixel tone ->
	// REQUIRES AlphaMode::Straight.
	Image contrast(const Image& src, float factor);

	// Raise the color channels to `exponent` (a power curve). Nonlinear per-pixel ->
	// REQUIRES AlphaMode::Straight; the result is left ColorSpace::Unspecified, because a
	// custom gamma is not a named standard.
	Image gamma(const Image& src, float exponent);

	// Clamp every channel into unit [lo, hi]. Per-pixel, space- and alpha-agnostic.
	Image clamp(const Image& src, float lo = 0.0f, float hi = 1.0f);

	// --- filters (cross-pixel) ---------------------------------------------------

	// Convolve with `kernel`, clamping at the edges. Cross-pixel blending -> REQUIRES
	// ColorSpace::Linear and (alpha-bearing formats) AlphaMode::Premultiplied.
	Image convolve(const Image& src, const Kernel& kernel);

	// Sharpen via a fixed 3x3 unsharp kernel — a convolve() preset (same requirements).
	Image sharpen(const Image& src);

	// --- resize ------------------------------------------------------------------

	// Resample to `extent` with the given Interpolation. Value-blending -> REQUIRES
	// ColorSpace::Linear and (alpha-bearing formats) AlphaMode::Premultiplied.
	Image resize(const Image& src, lain::math::Vec2i extent, Interpolation interp = Interpolation::Bilinear);

	// --- geometry ----------------------------------------------------------------

	// Extract the rectangular sub-region [x, x+w) x [y, y+h). Pixel-rearranging -> agnostic.
	Image crop(const Image& src, int x, int y, int w, int h);

	// Rotate by a multiple of 90 degrees clockwise (lossless, no resampling). Pixel-
	// rearranging -> agnostic. Extent swaps for odd quarter-turns.
	Image rotate90(const Image& src, int quarterTurns);

	// Rotate by an arbitrary angle (radians, clockwise) about the center, expanding the
	// canvas to the rotated bounding box; outside pixels are zero (transparent). Value-
	// blending -> REQUIRES ColorSpace::Linear and (alpha) AlphaMode::Premultiplied.
	Image rotate(const Image& src, float radians, Interpolation interp = Interpolation::Bilinear);
} // namespace lain::image
