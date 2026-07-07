#include "lain/image/convert.h"

#include "lain/image/colormath.h" // color-level convert + detail::toUnit / fromUnit
#include "lain/image/traverse.h"  // visit / transform

#include <lain/log/log.h>	 // log::ensure — the assert + log precondition guard
#include <lain/meta/enums.h> // enums::name for diagnostics

#include <cmath>
#include <type_traits>

namespace lain::image
{
	// --- file-local transfer helpers ---------------------------------------------

	static float srgbToLinear(float c)
	{
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}
	static float linearToSrgb(float c)
	{
		return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
	}

	// Apply a unit-[0,1] transfer function to a pixel's color channels — every channel but a
	// trailing alpha (which is linear and left as-is).
	template <typename View, typename Fn>
	static void mapColorChannels(View view, Fn fn)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr auto channels = descriptor(C::format).channelCount();
		constexpr glm::length_t colorChannels = descriptor(C::format).hasAlpha() ? channels - 1 : channels;
		for (auto& px : view)
		{
			for (glm::length_t i = 0; i < colorChannels; ++i)
				px[i] = detail::fromUnit<T>(fn(detail::toUnit<T>(px[i])));
		}
	}

	template <typename View>
	static void premultiplyView(View view)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr glm::length_t ch = descriptor(C::format).channelCount();
		for (auto& px : view)
		{
			const float a = detail::toUnit<T>(px[ch - 1]);
			for (glm::length_t i = 0; i < ch - 1; ++i)
				px[i] = detail::fromUnit<T>(detail::toUnit<T>(px[i]) * a);
		}
	}

	template <typename View>
	static void unpremultiplyView(View view)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr glm::length_t ch = descriptor(C::format).channelCount();
		for (auto& px : view)
		{
			const float a = detail::toUnit<T>(px[ch - 1]);
			if (a > 0.0f)
			{
				for (glm::length_t i = 0; i < ch - 1; ++i)
					px[i] = detail::fromUnit<T>(detail::toUnit<T>(px[i]) / a);
			}
		}
	}

	// --- convert to a PixelFormat -------------------------------------------------

	Image convert(const Image& src, PixelFormat dstFormat)
	{
		if (!src.valid())
			return {};

		// A reduction to Gray uses Rec709 luminance, which is only meaningful in linear light.
		const bool toGray = descriptor(dstFormat).model == ColorModel::Gray;
		if (toGray && src.descriptor().channelCount() >= 3)
		{
			if (!lain::log::ensure(src.colorSpace() == ColorSpace::Linear,
					"image::convert to Gray uses luminance and requires ColorSpace::Linear (got {})",
					lain::meta::enums::name(src.colorSpace())))
				return {};
		}

		// Tags carry through; a newly-added (opaque) alpha channel is Straight.
		AlphaMode dstAlpha = AlphaMode::Unspecified;
		if (descriptor(dstFormat).hasAlpha())
			dstAlpha = src.descriptor().hasAlpha() ? src.alphaMode() : AlphaMode::Straight;

		Image dst(src.width(), src.height(), dstFormat, src.colorSpace(), dstAlpha);
		visit(src, [&](auto sv)
			{
				visit(dst, [&](auto dv)
					{
						using DstC = std::remove_reference_t<decltype(dv(0, 0))>;
						transform(sv, dv, [](const auto& p) { return convert<DstC>(p); });
					});
			});
		return dst;
	}

	// --- convert to a ColorSpace --------------------------------------------------

	Image convert(const Image& src, ColorSpace dstSpace)
	{
		if (!src.valid())
			return {};
		if (!lain::log::ensure(dstSpace != ColorSpace::Unspecified,
				"image::convert target ColorSpace must not be Unspecified"))
			return {};

		const ColorSpace srcSpace = src.colorSpace();
		if (!lain::log::ensure(srcSpace != ColorSpace::Unspecified,
				"image::convert to {} needs a known source ColorSpace",
				lain::meta::enums::name(dstSpace)))
			return {};

		if (srcSpace == dstSpace) // already there
			return src;

		Image dst = src;
		dst.setColorSpace(dstSpace);
		if (srcSpace == ColorSpace::sRGB && dstSpace == ColorSpace::Linear)
			visit(dst, [](auto v) { mapColorChannels(v, srgbToLinear); });
		else if (srcSpace == ColorSpace::Linear && dstSpace == ColorSpace::sRGB)
			visit(dst, [](auto v) { mapColorChannels(v, linearToSrgb); });
		else if (!lain::log::ensure(false, "image::convert between {} and {} is not supported",
					 lain::meta::enums::name(srcSpace), lain::meta::enums::name(dstSpace)))
			return {};
		return dst;
	}

	// --- convert to an AlphaMode --------------------------------------------------

	Image convert(const Image& src, AlphaMode dstAlpha)
	{
		if (!src.valid())
			return {};
		if (!src.descriptor().hasAlpha()) // no alpha channel -> nothing to do
			return src;
		if (!lain::log::ensure(dstAlpha != AlphaMode::Unspecified,
				"image::convert target AlphaMode must not be Unspecified"))
			return {};

		const AlphaMode srcAlpha = src.alphaMode();
		if (!lain::log::ensure(srcAlpha != AlphaMode::Unspecified,
				"image::convert to {} needs a known source AlphaMode",
				lain::meta::enums::name(dstAlpha)))
			return {};

		if (srcAlpha == dstAlpha) // already there
			return src;

		Image dst = src;
		dst.setAlphaMode(dstAlpha);
		if (dstAlpha == AlphaMode::Premultiplied)
			visit(dst, [](auto v) { premultiplyView(v); });
		else
			visit(dst, [](auto v) { unpremultiplyView(v); });
		return dst;
	}
} // namespace lain::image
