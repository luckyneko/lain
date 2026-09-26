#include "lain/image/convert.h"

#include "lain/image/colormath.h"	  // color-level convert + detail::toUnit / fromUnit
#include "lain/image/transfercurve.h" // TransferCurve / transferCurve
#include "lain/image/traverse.h"	  // visit / transform

#include <lain/log/log.h>	 // log::ensure — the assert + log precondition guard
#include <lain/meta/enums.h> // enums::name for diagnostics

#include <type_traits>

namespace lain::image
{
	// The ColorSpace transfer (TransferCurve / transferCurve) lives in transfercurve.h; the
	// per-pixel-channel applicator (mapColorChannels) in colormath.h.

	template <typename View>
	static void premultiplyView(View view)
	{
		using C = std::remove_reference_t<decltype(view(0, 0))>;
		using T = typename C::value_type;
		constexpr math::length_t ch = formatDescriptor(C::format).channelCount();
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
		constexpr math::length_t ch = formatDescriptor(C::format).channelCount();
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
		const ColorModel dstModel = formatDescriptor(dstFormat).model;
		const bool toGray = dstModel == ColorModel::Gray || dstModel == ColorModel::GrayAlpha;
		if (toGray && src.formatDescriptor().channelCount() >= 3)
		{
			if (!lain::log::ensure(src.colorSpace() == ColorSpace::Linear,
								   "image::convert to Gray uses luminance and requires ColorSpace::Linear (got {})",
								   lain::meta::enums::name(src.colorSpace())))
				return {};
		}

		// Tags carry through; a newly-added (opaque) alpha channel is Straight.
		AlphaMode dstAlpha = AlphaMode::Unspecified;
		if (formatDescriptor(dstFormat).hasAlpha())
			dstAlpha = src.formatDescriptor().hasAlpha() ? src.alphaMode() : AlphaMode::Straight;

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

		// Already there, and this early return is load-bearing rather than an optimisation: a
		// decode followed by its own encode is NOT an identity, because both are pow curves with
		// the spec's rounded constants. Measured over a 1001-point sweep, 314 sRGB values and 412
		// BT709 values do not land back where they started. So converting to the space an image is
		// already in must return it untouched here; nothing below could.
		if (srcSpace == dstSpace)
			return src;

		// Compose through linear light rather than pairing spaces: decode out of the source
		// curve, encode into the destination's. That expresses EVERY pairing (so there is no
		// unsupported-pair arm to fall off), and it does so in ONE pass — the intermediate
		// stays a float within a single channel visit, so an 8-bit image is quantised once
		// instead of once per hop.
		//
		// Both curves are resolved ONCE here rather than per colour channel. That is a structural
		// tidy, NOT a speedup: measured over 50M channels, hoisting is within noise of switching
		// per channel (0.995-1.012x across runs), because two pow calls dominate everything around
		// them. What it buys is that the pair is named once and used twice, which is the point of
		// carrying a transfer as a value.
		const TransferCurve decode = transferCurve(srcSpace);
		const TransferCurve encode = transferCurve(dstSpace);

		Image dst = src;
		dst.setColorSpace(dstSpace);
		visit(dst, [decode, encode](auto v)
			  { mapColorChannels(v, [decode, encode](float c)
								 { return encode.fromLinear(decode.toLinear(c)); }); });
		return dst;
	}

	// --- convert to an AlphaMode --------------------------------------------------

	Image convert(const Image& src, AlphaMode dstAlpha)
	{
		if (!src.valid())
			return {};
		if (!src.formatDescriptor().hasAlpha()) // no alpha channel -> nothing to do
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
