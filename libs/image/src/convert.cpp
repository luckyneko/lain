#include "lain/image/convert.h"

#include "lain/image/colormath.h" // color-level convert + detail::toUnit / fromUnit
#include "lain/image/traverse.h"  // visit / transform

#include <lain/log/log.h>	 // log::ensure — the assert + log precondition guard
#include <lain/meta/enums.h> // enums::name for diagnostics

#include <type_traits>

namespace lain::image
{
	// The transfer curves (toLinear / fromLinear) and the per-pixel-channel applicator
	// (mapColorChannels) both live in colormath.h.

	template <typename View>
	static void premultiplyView(View view)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr math::length_t ch = descriptor(C::format).channelCount();
		for (auto& px : view)
		{
			const float a = detail::toUnit<T>(px[ch - 1]);
			for (math::length_t i = 0; i < ch - 1; ++i)
				px[i] = detail::fromUnit<T>(detail::toUnit<T>(px[i]) * a);
		}
	}

	template <typename View>
	static void unpremultiplyView(View view)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr math::length_t ch = descriptor(C::format).channelCount();
		for (auto& px : view)
		{
			const float a = detail::toUnit<T>(px[ch - 1]);
			if (a > 0.0f)
			{
				for (math::length_t i = 0; i < ch - 1; ++i)
					px[i] = detail::fromUnit<T>(detail::toUnit<T>(px[i]) / a);
			}
		}
	}

	// --- convert to a PixelFormat -------------------------------------------------

	Image convert(const Image& src, PixelFormat dstFormat)
	{
		if (!src.valid())
			return {};

		// A reduction to Gray / GrayAlpha uses Rec709 luminance, only meaningful in linear light.
		const ColorModel dstModel = descriptor(dstFormat).model;
		const bool toGray = dstModel == ColorModel::Gray || dstModel == ColorModel::GrayAlpha;
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
			  { visit(dst, [&](auto dv)
					  {
						using DstC = std::remove_reference_t<decltype(dv(0, 0))>;
						transform(sv, dv, [](const auto& p) { return convert<DstC>(p); }); }); });
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

		// Compose through linear light rather than pairing spaces: decode out of the source
		// curve, encode into the destination's. That expresses EVERY pairing (so there is no
		// unsupported-pair arm to fall off), and it does so in ONE pass — the intermediate
		// stays a float within a single channel visit, so an 8-bit image is quantised once
		// instead of once per hop.
		Image dst = src;
		dst.setColorSpace(dstSpace);
		visit(dst, [srcSpace, dstSpace](auto v)
			  { mapColorChannels(v, [srcSpace, dstSpace](float c)
								 { return fromLinear(dstSpace, toLinear(srcSpace, c)); }); });
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
			visit(dst, [](auto v)
				  { premultiplyView(v); });
		else
			visit(dst, [](auto v)
				  { unpremultiplyView(v); });
		return dst;
	}
} // namespace lain::image
