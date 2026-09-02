#pragma once

// Definitions for the color-value algorithms (see colormath.h): channel-value normalization
// across base types, luminance, saturate, the Color -> Color convert, and the ColorSpace
// transfer curves. The transfer functions are non-template but live here (inline) rather than
// in a .cpp so they still inline into mapColorChannels — they run once per color channel of
// every converted image, so an out-of-line call there is a real cost.

#include <cmath>
#include <limits>
#include <type_traits>

namespace lain::image
{
	namespace detail
	{
		// A channel value in unit [0,1] space: integral divides by its max, float passes
		// through (already unit by convention).
		template <typename T>
		float toUnit(T v)
		{
			if constexpr (std::is_floating_point_v<T>)
				return static_cast<float>(v);
			else
				return static_cast<float>(v) / static_cast<float>(std::numeric_limits<T>::max());
		}

		// A unit-[0,1] value back into channel type U: integral clamps, scales and rounds;
		// float passes through.
		template <typename U>
		U fromUnit(float f)
		{
			if constexpr (std::is_floating_point_v<U>)
			{
				return static_cast<U>(f);
			}
			else
			{
				const float clamped = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
				return static_cast<U>(clamped * static_cast<float>(std::numeric_limits<U>::max()) + 0.5f);
			}
		}
	} // namespace detail

	template <PixelFormat F>
	float luminance(const Color<F>& c)
	{
		using T = typename Color<F>::value_type;
		constexpr auto N = descriptor(F).channelCount();
		if constexpr (N == 1 || N == 2)
			return detail::toUnit<T>(c[0]); // Gray / GrayAlpha: channel 0 is the luminance
		else
			return 0.2126f * detail::toUnit<T>(c[0]) + 0.7152f * detail::toUnit<T>(c[1]) + 0.0722f * detail::toUnit<T>(c[2]);
	}

	template <PixelFormat F>
	Color<F> saturate(const Color<F>& c)
	{
		using T = typename Color<F>::value_type;
		constexpr auto N = descriptor(F).channelCount();
		Color<F> out{};
		for (math::length_t i = 0; i < N; ++i)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				const T v = c[i];
				out[i] = v < T(0) ? T(0) : (v > T(1) ? T(1) : v);
			}
			else
			{
				out[i] = c[i]; // integral channels are already within [0, max]
			}
		}
		return out;
	}

	template <typename Dst, PixelFormat Src>
	Dst convert(const Color<Src>& src)
	{
		// Exact identity: skip the unit round-trip (which could shed a least-significant bit).
		if constexpr (std::is_same_v<Dst, Color<Src>>)
		{
			return src;
		}
		else
		{
			using T = typename Color<Src>::value_type;
			using U = typename Dst::value_type;
			constexpr auto N = descriptor(Src).channelCount();
			constexpr auto M = descriptor(Dst::format).channelCount();

			// source channels -> canonical unit RGBA
			float r, g, b, a;
			if constexpr (N == 1)
			{
				const float u = detail::toUnit<T>(src[0]);
				r = g = b = u;
				a = 1.0f;
			}
			else if constexpr (N == 2)
			{
				const float u = detail::toUnit<T>(src[0]); // GrayAlpha: luminance + alpha
				r = g = b = u;
				a = detail::toUnit<T>(src[1]);
			}
			else if constexpr (N == 3)
			{
				r = detail::toUnit<T>(src[0]);
				g = detail::toUnit<T>(src[1]);
				b = detail::toUnit<T>(src[2]);
				a = 1.0f;
			}
			else
			{
				r = detail::toUnit<T>(src[0]);
				g = detail::toUnit<T>(src[1]);
				b = detail::toUnit<T>(src[2]);
				a = detail::toUnit<T>(src[3]);
			}

			// canonical unit RGBA -> destination channels
			Dst out{};
			if constexpr (M == 1)
			{
				out[0] = detail::fromUnit<U>(0.2126f * r + 0.7152f * g + 0.0722f * b);
			}
			else if constexpr (M == 2)
			{
				out[0] = detail::fromUnit<U>(0.2126f * r + 0.7152f * g + 0.0722f * b); // GrayAlpha: luminance + alpha
				out[1] = detail::fromUnit<U>(a);
			}
			else if constexpr (M == 3)
			{
				out[0] = detail::fromUnit<U>(r);
				out[1] = detail::fromUnit<U>(g);
				out[2] = detail::fromUnit<U>(b);
			}
			else
			{
				out[0] = detail::fromUnit<U>(r);
				out[1] = detail::fromUnit<U>(g);
				out[2] = detail::fromUnit<U>(b);
				out[3] = detail::fromUnit<U>(a);
			}
			return out;
		}
	}

	template <typename Dst>
	Dst convert(const ColorHSVf& hsv)
	{
		float h = std::fmod(hsv.h, 360.0f);
		if (h < 0.0f)
			h += 360.0f;
		const float s = hsv.s < 0.0f ? 0.0f : (hsv.s > 1.0f ? 1.0f : hsv.s);
		const float v = hsv.v < 0.0f ? 0.0f : (hsv.v > 1.0f ? 1.0f : hsv.v);

		// Standard HSV -> RGB: chroma c, second-largest component x, and the match m added to all.
		const float c = v * s;
		const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
		const float m = v - c;
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		if (h < 60.0f)
		{
			r = c;
			g = x;
		}
		else if (h < 120.0f)
		{
			r = x;
			g = c;
		}
		else if (h < 180.0f)
		{
			g = c;
			b = x;
		}
		else if (h < 240.0f)
		{
			g = x;
			b = c;
		}
		else if (h < 300.0f)
		{
			r = x;
			b = c;
		}
		else
		{
			r = c;
			b = x;
		}
		return convert<Dst>(ColorRGBf(r + m, g + m, b + m));
	}

	// --- ColorSpace transfer curves ----------------------------------------------
	//
	// Each pair is a standard's own encode/decode, over unit [0,1] channel values. Both use
	// their spec's ROUNDED constants — sRGB's 0.04045 / 1.055 / 2.4 and Rec.709's
	// 4.5 / 0.018 / 1.099 / 0.099, as the documents print them and as OCIO / Nuke spell them.
	// Neither pair's two segments therefore meet exactly (Rec.709 steps by ~2e-4 at its
	// breakpoint); that is the published curve, so do NOT "fix" it to the continuous alpha /
	// beta values — that silently changes every decode for a discontinuity nothing can see.
	namespace detail
	{
		inline float srgbToLinear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : math::pow((c + 0.055f) / 1.055f, 2.4f);
		}
		inline float linearToSrgb(float c)
		{
			return c <= 0.0031308f ? c * 12.92f : 1.055f * math::pow(c, 1.0f / 2.4f) - 0.055f;
		}

		// The INVERSE Rec.709 OETF and the OETF itself — the curve a camera/encoder actually
		// applied, which is what "Rec709 to linear" means in Nuke and OCIO (ADR-0018). NOT the
		// BT.1886 2.4 display gamma: lain applies no OOTF and does no display rendering.
		inline float bt709ToLinear(float c)
		{
			return c <= 0.081f ? c / 4.5f : math::pow((c + 0.099f) / 1.099f, 1.0f / 0.45f);
		}
		inline float linearToBt709(float c)
		{
			return c <= 0.018f ? c * 4.5f : 1.099f * math::pow(c, 0.45f) - 0.099f;
		}
	} // namespace detail

	inline float toLinear(ColorSpace space, float c)
	{
		switch (space)
		{
			case ColorSpace::sRGB:
				return detail::srgbToLinear(c);
			case ColorSpace::BT709:
				return detail::bt709ToLinear(c);
			case ColorSpace::Linear:
			case ColorSpace::Unspecified:
				break;
		}
		return c;
	}

	inline float fromLinear(ColorSpace space, float c)
	{
		switch (space)
		{
			case ColorSpace::sRGB:
				return detail::linearToSrgb(c);
			case ColorSpace::BT709:
				return detail::linearToBt709(c);
			case ColorSpace::Linear:
			case ColorSpace::Unspecified:
				break;
		}
		return c;
	}

	template <typename View, typename Fn>
	void mapColorChannels(View view, Fn fn)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr auto channels = descriptor(C::format).channelCount();
		constexpr math::length_t colorChannels = descriptor(C::format).hasAlpha() ? channels - 1 : channels;
		for (auto& px : view)
		{
			for (math::length_t i = 0; i < colorChannels; ++i)
				px[i] = detail::fromUnit<T>(fn(detail::toUnit<T>(px[i])));
		}
	}
} // namespace lain::image
