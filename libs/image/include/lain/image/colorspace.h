#pragma once

namespace lain::image
{
	// The transfer / encoding a pixel's values carry — the color-space axis, tracked on an
	// Image and *enforced* by space-sensitive ops. Deliberately SEPARATE from PixelFormat
	// (which is layout only): sRGB and linear share a byte layout but not a meaning.
	// Conversions are always explicit (image::convert(img, ColorSpace)) and update the tag — no op
	// auto-converts (round trips shed accuracy). Values name unambiguous standards; a
	// custom/parametric curve is an op argument (image::gamma), never tracked here. Default
	// is Unspecified: be explicit before a space-sensitive op, or it asserts.
	//
	// This axis is TRANSFER ONLY — it says nothing about primaries or gamut. sRGB and BT709
	// share primaries and differ only in their curve, which is exactly why the tag is needed:
	// the two are byte-identical and visually close, so a mislabel is silent. (Do not confuse
	// BT709-the-space with image::luminance's "Rec709 luminance", which is the primaries'
	// matrix and applies in linear light whatever this tag says.)
	//
	// Adding a standard is additive: image::convert composes through Linear, so a new value
	// needs one curve pair in colormath.h and no new dispatch anywhere.
	enum class ColorSpace
	{
		Unspecified, // undeclared — asserts on any space-sensitive op
		Linear,		 // linear light — the correct space for blending / filtering
		sRGB,		 // sRGB-encoded — display-ready
		BT709,		 // Rec.709-encoded — the transfer video decodes against (see ADR-0018)
	};

	// Whether an alpha-bearing pixel's color channels are premultiplied by alpha. Tracked
	// on an Image and enforced by alpha-sensitive ops; meaningful only for formats that
	// carry an alpha channel. Explicit image::convert(img, AlphaMode) updates the tag.
	// Default Unspecified — be explicit.
	enum class AlphaMode
	{
		Unspecified,   // undeclared — asserts on any alpha-sensitive op
		Straight,	   // unassociated: color is independent of alpha
		Premultiplied, // associated: color already scaled by alpha
	};
} // namespace lain::image
