#include "lain/image/operations.h"

#include "lain/image/colormath.h" // mapColorChannels + detail::toUnit / fromUnit
#include "lain/image/traverse.h"  // visit

#include <lain/log/log.h>	 // log::ensure
#include <lain/meta/enums.h> // enums::name for diagnostics

#include <cstddef>
#include <type_traits>

namespace lain::image
{
	// --- shared helpers ----------------------------------------------------------

	// True when the format has no alpha channel (alpha mode is moot) or its mode matches.
	static bool alphaOk(const Image& img, AlphaMode required)
	{
		return !img.descriptor().hasAlpha() || img.alphaMode() == required;
	}

	// A value-blending op needs linear light and (for alpha formats) premultiplied color.
	static bool ensureBlendable(const Image& src, const char* op)
	{
		return lain::log::ensure(src.colorSpace() == ColorSpace::Linear,
				   "image::{} is a value-blending op and requires ColorSpace::Linear (got {})",
				   op, lain::meta::enums::name(src.colorSpace()))
			&& lain::log::ensure(alphaOk(src, AlphaMode::Premultiplied),
				   "image::{} blends alpha and requires AlphaMode::Premultiplied (got {})",
				   op, lain::meta::enums::name(src.alphaMode()));
	}

	static int clampInt(int v, int lo, int hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	// Sample a view at continuous (fx, fy) with the given Interpolation, returning a pixel.
	// Coordinates are in pixel space (a pixel center is at integer+0.5); taps clamp at edges.
	template <typename View>
	static auto samplePixel(const View& view, float fx, float fy, Interpolation interp)
	{
		using C = std::remove_const_t<std::remove_reference_t<decltype(view(0, 0))>>;
		using T = typename C::value_type;
		constexpr math::length_t ch = descriptor(C::format).channelCount();
		const int w = view.width();
		const int h = view.height();

		if (interp == Interpolation::Nearest)
		{
			const int sx = clampInt(static_cast<int>(math::floor(fx)), 0, w - 1);
			const int sy = clampInt(static_cast<int>(math::floor(fy)), 0, h - 1);
			return C(view(sx, sy));
		}

		const float gx = fx - 0.5f;
		const float gy = fy - 0.5f;
		const int x0 = clampInt(static_cast<int>(math::floor(gx)), 0, w - 1);
		const int x1 = clampInt(x0 + 1, 0, w - 1);
		const int y0 = clampInt(static_cast<int>(math::floor(gy)), 0, h - 1);
		const int y1 = clampInt(y0 + 1, 0, h - 1);
		const float tx = gx - math::floor(gx);
		const float ty = gy - math::floor(gy);

		C out{};
		for (math::length_t c = 0; c < ch; ++c)
		{
			const float a = detail::toUnit<T>(view(x0, y0)[c]);
			const float b = detail::toUnit<T>(view(x1, y0)[c]);
			const float cc = detail::toUnit<T>(view(x0, y1)[c]);
			const float d = detail::toUnit<T>(view(x1, y1)[c]);
			const float top = a + (b - a) * tx;
			const float bot = cc + (d - cc) * tx;
			out[c] = detail::fromUnit<T>(top + (bot - top) * ty);
		}
		return out;
	}

	// --- interpolation & kernels -------------------------------------------------

	Kernel gaussianKernel(int radius, float sigma)
	{
		Kernel k;
		k.radius = radius < 0 ? 0 : radius;
		const int n = k.size();
		k.weights.resize(static_cast<std::size_t>(n) * n);
		const float twoSigma2 = 2.0f * sigma * sigma;
		float sum = 0.0f;
		for (int y = -k.radius; y <= k.radius; ++y)
		{
			for (int x = -k.radius; x <= k.radius; ++x)
			{
				const float v = math::exp(-static_cast<float>(x * x + y * y) / twoSigma2);
				k.weights[static_cast<std::size_t>((y + k.radius) * n + (x + k.radius))] = v;
				sum += v;
			}
		}
		for (float& w : k.weights)
			w /= sum;
		return k;
	}

	// --- per-pixel tone ----------------------------------------------------------

	Image brightness(const Image& src, float factor)
	{
		if (!src.valid())
			return {};
		Image dst = src;
		visit(dst, [factor](auto v)
			{ mapColorChannels(v, [factor](float c) { return c * factor; }); });
		return dst;
	}

	Image contrast(const Image& src, float factor)
	{
		if (!src.valid())
			return {};
		if (!lain::log::ensure(alphaOk(src, AlphaMode::Straight),
				"image::contrast requires AlphaMode::Straight (got {})",
				lain::meta::enums::name(src.alphaMode())))
			return {};
		Image dst = src;
		visit(dst, [factor](auto v)
			{ mapColorChannels(v, [factor](float c) { return (c - 0.5f) * factor + 0.5f; }); });
		return dst;
	}

	Image gamma(const Image& src, float exponent)
	{
		if (!src.valid())
			return {};
		if (!lain::log::ensure(alphaOk(src, AlphaMode::Straight),
				"image::gamma requires AlphaMode::Straight (got {})",
				lain::meta::enums::name(src.alphaMode())))
			return {};
		Image dst = src;
		dst.setColorSpace(ColorSpace::Unspecified); // a custom gamma is not a named standard
		visit(dst, [exponent](auto v)
			{ mapColorChannels(v, [exponent](float c) { return math::pow(c, exponent); }); });
		return dst;
	}

	Image clamp(const Image& src, float lo, float hi)
	{
		if (!src.valid())
			return {};
		Image dst = src;
		visit(dst, [lo, hi](auto v)
			{
				using C = std::remove_reference_t<decltype(v(0, 0))>;
				using T = typename C::value_type;
				constexpr math::length_t ch = descriptor(C::format).channelCount();
				for (auto& px : v)
				{
					for (math::length_t i = 0; i < ch; ++i)
					{
						const float c = detail::toUnit<T>(px[i]);
						px[i] = detail::fromUnit<T>(c < lo ? lo : (c > hi ? hi : c));
					}
				}
			});
		return dst;
	}

	// --- filters (cross-pixel) ---------------------------------------------------

	Image convolve(const Image& src, const Kernel& kernel)
	{
		if (!src.valid())
			return {};
		if (!ensureBlendable(src, "convolve"))
			return {};
		Image dst(src.width(), src.height(), src.pixelFormat(), src.colorSpace(), src.alphaMode());
		visit(src, [&](auto sv)
			{
				using C = std::remove_const_t<std::remove_reference_t<decltype(sv(0, 0))>>;
				using T = typename C::value_type;
				constexpr math::length_t ch = descriptor(C::format).channelCount();
				auto dv = dst.as<C>();
				const int w = sv.width();
				const int h = sv.height();
				for (int y = 0; y < h; ++y)
				{
					for (int x = 0; x < w; ++x)
					{
						float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
						for (int ky = -kernel.radius; ky <= kernel.radius; ++ky)
						{
							for (int kx = -kernel.radius; kx <= kernel.radius; ++kx)
							{
								const int sx = clampInt(x + kx, 0, w - 1);
								const int sy = clampInt(y + ky, 0, h - 1);
								const float wt = kernel.at(kx, ky);
								const auto& sp = sv(sx, sy);
								for (math::length_t c = 0; c < ch; ++c)
									acc[c] += wt * detail::toUnit<T>(sp[c]);
							}
						}
						auto& dp = dv(x, y);
						for (math::length_t c = 0; c < ch; ++c)
							dp[c] = detail::fromUnit<T>(acc[c]);
					}
				}
			});
		return dst;
	}

	Image sharpen(const Image& src)
	{
		const Kernel k{1, {0.0f, -1.0f, 0.0f, -1.0f, 5.0f, -1.0f, 0.0f, -1.0f, 0.0f}};
		return convolve(src, k);
	}

	// --- resize ------------------------------------------------------------------

	Image resize(const Image& src, lain::math::Vec2i extent, Interpolation interp)
	{
		if (!src.valid() || extent.x <= 0 || extent.y <= 0)
			return {};
		if (!ensureBlendable(src, "resize"))
			return {};
		Image dst(extent.x, extent.y, src.pixelFormat(), src.colorSpace(), src.alphaMode());
		visit(src, [&](auto sv)
			{
				using C = std::remove_const_t<std::remove_reference_t<decltype(sv(0, 0))>>;
				auto dv = dst.as<C>();
				const float sxScale = static_cast<float>(sv.width()) / extent.x;
				const float syScale = static_cast<float>(sv.height()) / extent.y;
				for (int y = 0; y < extent.y; ++y)
				{
					for (int x = 0; x < extent.x; ++x)
						dv(x, y) = samplePixel(sv, (x + 0.5f) * sxScale, (y + 0.5f) * syScale, interp);
				}
			});
		return dst;
	}

	// --- geometry ----------------------------------------------------------------

	Image crop(const Image& src, int x, int y, int w, int h)
	{
		if (!src.valid())
			return {};
		if (!lain::log::ensure(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= src.width() && y + h <= src.height(),
				"image::crop rect ({},{} {}x{}) is out of bounds for a {}x{} image",
				x, y, w, h, src.width(), src.height()))
			return {};
		Image dst(w, h, src.pixelFormat(), src.colorSpace(), src.alphaMode());
		visit(src, [&](auto sv)
			{
				using C = std::remove_const_t<std::remove_reference_t<decltype(sv(0, 0))>>;
				auto dv = dst.as<C>();
				const auto window = sv.subview(x, y, w, h);
				for (int yy = 0; yy < h; ++yy)
				{
					for (int xx = 0; xx < w; ++xx)
						dv(xx, yy) = window(xx, yy);
				}
			});
		return dst;
	}

	Image rotate90(const Image& src, int quarterTurns)
	{
		if (!src.valid())
			return {};
		const int t = ((quarterTurns % 4) + 4) % 4;
		if (t == 0)
			return src;
		const bool swap = (t == 1 || t == 3);
		const int dw = swap ? src.height() : src.width();
		const int dh = swap ? src.width() : src.height();
		Image dst(dw, dh, src.pixelFormat(), src.colorSpace(), src.alphaMode());
		visit(src, [&](auto sv)
			{
				using C = std::remove_const_t<std::remove_reference_t<decltype(sv(0, 0))>>;
				auto dv = dst.as<C>();
				const int w = sv.width();
				const int h = sv.height();
				for (int y = 0; y < h; ++y)
				{
					for (int x = 0; x < w; ++x)
					{
						int nx = 0;
						int ny = 0;
						if (t == 1) // 90 clockwise
						{
							nx = h - 1 - y;
							ny = x;
						}
						else if (t == 2) // 180
						{
							nx = w - 1 - x;
							ny = h - 1 - y;
						}
						else // 270 clockwise
						{
							nx = y;
							ny = w - 1 - x;
						}
						dv(nx, ny) = sv(x, y);
					}
				}
			});
		return dst;
	}

	Image rotate(const Image& src, float radians, Interpolation interp)
	{
		if (!src.valid())
			return {};
		if (!ensureBlendable(src, "rotate"))
			return {};
		const float cs = math::cos(radians);
		const float sn = math::sin(radians);
		const int sw = src.width();
		const int sh = src.height();
		const int dw = static_cast<int>(math::ceil(math::abs(sw * cs) + math::abs(sh * sn)));
		const int dh = static_cast<int>(math::ceil(math::abs(sw * sn) + math::abs(sh * cs)));
		Image dst(dw, dh, src.pixelFormat(), src.colorSpace(), src.alphaMode()); // zero-filled outside
		visit(src, [&](auto sv)
			{
				using C = std::remove_const_t<std::remove_reference_t<decltype(sv(0, 0))>>;
				auto dv = dst.as<C>();
				const float scx = sw * 0.5f;
				const float scy = sh * 0.5f;
				const float dcx = dw * 0.5f;
				const float dcy = dh * 0.5f;
				for (int y = 0; y < dh; ++y)
				{
					for (int x = 0; x < dw; ++x)
					{
						const float rx = x + 0.5f - dcx;
						const float ry = y + 0.5f - dcy;
						const float sxf = (cs * rx + sn * ry) + scx;
						const float syf = (-sn * rx + cs * ry) + scy;
						if (sxf >= 0.0f && sxf < sw && syf >= 0.0f && syf < sh)
							dv(x, y) = samplePixel(sv, sxf, syf, interp);
					}
				}
			});
		return dst;
	}
} // namespace lain::image
