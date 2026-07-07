#pragma once

namespace lain::image
{
	// The transfer / encoding a pixel's values carry — the color-space axis, tracked on an
	// Image and *enforced* by space-sensitive ops. Deliberately SEPARATE from PixelFormat
	// (which is layout only): sRGB and linear share a byte layout but not a meaning.
	// Conversions are always explicit (image::toLinear / toSRGB) and update the tag — no op
	// auto-converts (round trips shed accuracy). Values name unambiguous standards; a
	// custom/parametric curve is an op argument (image::gamma), never tracked here. Default
	// is Unspecified: be explicit before a space-sensitive op, or it asserts.
	enum class ColorSpace
	{
		Unspecified, // undeclared — asserts on any space-sensitive op
		Linear,		 // linear light — the correct space for blending / filtering
		sRGB,		 // sRGB-encoded — display-ready
	};

	// Whether an alpha-bearing pixel's color channels are premultiplied by alpha. Tracked
	// on an Image and enforced by alpha-sensitive ops; meaningful only for formats that
	// carry an alpha channel. Explicit image::premultiply / unpremultiply update the tag.
	// Default Unspecified — be explicit.
	enum class AlphaMode
	{
		Unspecified,   // undeclared — asserts on any alpha-sensitive op
		Straight,	   // unassociated: color is independent of alpha
		Premultiplied, // associated: color already scaled by alpha
	};
} // namespace lain::image
