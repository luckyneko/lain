#pragma once

// Template definitions for the color-value algorithms (see colormath.h): channel-value
// normalization across base types, luminance, saturate, and the Color -> Color convert.

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
		if constexpr (N == 1)
			return detail::toUnit<T>(c[0]);
		else
			return 0.2126f * detail::toUnit<T>(c[0]) + 0.7152f * detail::toUnit<T>(c[1]) + 0.0722f * detail::toUnit<T>(c[2]);
	}

	template <PixelFormat F>
	Color<F> saturate(const Color<F>& c)
	{
		using T = typename Color<F>::value_type;
		constexpr auto N = descriptor(F).channelCount();
		Color<F> out{};
		for (glm::length_t i = 0; i < N; ++i)
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
} // namespace lain::image
