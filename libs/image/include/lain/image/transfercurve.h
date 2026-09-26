#pragma once

#include "lain/image/colorspace.h" // ColorSpace — the axis a TransferCurve belongs to

// The transfer of each tracked ColorSpace: the curves relating a space's encoded values to linear
// light. Its own header because its consumers and colormath.h's are nearly disjoint, and not part
// of colorspace.h beside the enum because these need math::pow where that header has zero includes.
namespace lain::image
{
	// A ColorSpace's transfer, as ONE value: two halves you get together or not at all. That
	// indivisibility is the point — two free functions can be half-written, reversed, or drift apart
	// across two switches that must agree. Default-constructs to the identity, so a curve is never
	// null and no caller checks for one.
	//
	// Resolve it once per asset rather than per channel: readability, not speed — measured within
	// noise of switching per channel, since a pow dominates either way (the number is in convert.cpp).
	struct TransferCurve
	{
		static constexpr float unchanged(float c) { return c; }
		float (*toLinear)(float) = unchanged;	// the space's encoded value -> linear light
		float (*fromLinear)(float) = unchanged; // linear light -> the space's encoded value
	};

	// The curve a ColorSpace transfers through, in unit [0,1] channel space. TOTAL — every enumerator
	// answers a usable pair. Conversions compose THROUGH linear light, which is what keeps adding a
	// standard to one arm here and no new dispatch anywhere.
	//
	// Linear is the identity, as that composition requires. Unspecified is the identity too: a
	// fallback so this stays total, NOT a meaning — a caller that cares rejects it first, as
	// image::convert does. (lain::log is PRIVATE to this target, so this header cannot assert.)
	//
	// The alternatives weighed and the triggers for changing shape — an X-macro table, a
	// ColorSpaceDescriptor around the pair, qualifying this name — are in WORK.md's "A ColorSpace's
	// transfer becomes one value" and ADR-0003. Read those before reshaping this.
	TransferCurve transferCurve(ColorSpace space);
} // namespace lain::image

#include "lain/image/details/transfercurve.inl"
