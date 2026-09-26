#pragma once

// Definitions for the ColorSpace transfer (see transfercurve.h). The curves are non-template but
// live here (inline) rather than in a .cpp because they run once per color channel of every
// converted image, so an out-of-line call there is a real cost. transferCurve itself runs once per
// asset and would not care.

#include <lain/math/types.h> // math::pow — the curves' only dependency

#include <type_traits>

namespace lain::image
{
	// What the brace-built pair constants below and the by-value copy per asset rely on.
	static_assert(std::is_aggregate_v<TransferCurve>, "TransferCurve must stay brace-initialisable");
	static_assert(std::is_trivially_copyable_v<TransferCurve>, "TransferCurve must stay a value");

	namespace detail
	{
		// Each pair is a standard's own decode/encode over unit [0,1] values, using its spec's ROUNDED
		// constants as the documents print them and as OCIO / Nuke spell them. The two segments of a
		// pair therefore do not meet exactly (Rec.709 steps by ~2e-4 at its breakpoint): that is the
		// published curve, so do NOT "fix" it to the continuous alpha / beta values — it would change
		// every decode for a discontinuity nothing can see.
		//
		// Space first, direction last, so each pair sorts adjacent and the suffixes mirror
		// TransferCurve's members — a pair built in the wrong order reads as wrong.
		inline float srgbToLinear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : math::pow((c + 0.055f) / 1.055f, 2.4f);
		}
		inline float srgbFromLinear(float c)
		{
			return c <= 0.0031308f ? c * 12.92f : 1.055f * math::pow(c, 1.0f / 2.4f) - 0.055f;
		}
		inline constexpr TransferCurve kSrgb{srgbToLinear, srgbFromLinear};

		// The INVERSE Rec.709 OETF and the OETF itself — the curve a camera/encoder actually applied,
		// which is what "Rec709 to linear" means in Nuke and OCIO (ADR-0018). NOT the BT.1886 2.4
		// display gamma: lain applies no OOTF and does no display rendering.
		inline float bt709ToLinear(float c)
		{
			return c <= 0.081f ? c / 4.5f : math::pow((c + 0.099f) / 1.099f, 1.0f / 0.45f);
		}
		inline float bt709FromLinear(float c)
		{
			return c <= 0.018f ? c * 4.5f : 1.099f * math::pow(c, 0.45f) - 0.099f;
		}
		inline constexpr TransferCurve kBt709{bt709ToLinear, bt709FromLinear};
	} // namespace detail

	inline TransferCurve transferCurve(ColorSpace space)
	{
		// Exhaustive with NO default arm, deliberately: every other ColorSpace site in lain::image is an
		// == comparison, so this switch is what makes adding a standard fail the build
		// (-Werror,-Wswitch) rather than show up later as a wrong picture. Adding `default:` would
		// silence nothing today and delete that guard — do not "tidy" it in.
		switch (space)
		{
			case ColorSpace::sRGB:
				return detail::kSrgb;
			case ColorSpace::BT709:
				return detail::kBt709;
			case ColorSpace::Linear:	  // linear light already — nothing to decode or encode
			case ColorSpace::Unspecified: // no stated transfer; the total-function fallback
				break;
		}
		return TransferCurve();
	}
} // namespace lain::image
