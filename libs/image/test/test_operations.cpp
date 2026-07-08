// Unit tests for the image operation catalog — tone, filters, resize, geometry. Ops that
// enforce a space/alpha precondition assert in debug, so those bail cases are NDEBUG-only.

#include "lain/image/color.h"
#include "lain/image/imageview.h"
#include "lain/image/operations.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace lain::image;

// --- per-pixel tone ----------------------------------------------------------

TEST_CASE("brightness scales color channels and leaves alpha", "[ops]")
{
	Image img(1, 1, PixelFormat::RGBA8);
	img.as<ColorRGBA8>()(0, 0) = ColorRGBA8(10, 20, 30, 40);
	const ColorRGBA8 p = brightness(img, 2.0f).as<ColorRGBA8>()(0, 0);
	REQUIRE(p.r == 20);
	REQUIRE(p.g == 40);
	REQUIRE(p.b == 60);
	REQUIRE(p.a == 40); // alpha untouched
}

TEST_CASE("brightness clamps integer channels at the max", "[ops]")
{
	Image img(1, 1, PixelFormat::RGB8);
	img.as<ColorRGB8>()(0, 0) = ColorRGB8(200, 128, 0);
	const ColorRGB8 p = brightness(img, 2.0f).as<ColorRGB8>()(0, 0);
	REQUIRE(p.r == 255); // 200*2 clamps
	REQUIRE(p.b == 0);
}

TEST_CASE("contrast scales around mid-gray", "[ops]")
{
	Image img(1, 1, PixelFormat::RGBA32F, ColorSpace::Linear, AlphaMode::Straight);
	img.as<ColorRGBAf>()(0, 0) = ColorRGBAf(0.75f, 0.25f, 0.5f, 1.0f);
	const ColorRGBAf p = contrast(img, 2.0f).as<ColorRGBAf>()(0, 0);
	REQUIRE(p.r == Approx(1.0f)); // (0.75-0.5)*2+0.5
	REQUIRE(p.g == Approx(0.0f)); // (0.25-0.5)*2+0.5
	REQUIRE(p.b == Approx(0.5f)); // mid-gray is the fixed point
}

TEST_CASE("gamma applies a power curve and unsets the color space", "[ops]")
{
	Image img(1, 1, PixelFormat::RGB32F, ColorSpace::Linear);
	img.as<ColorRGBf>()(0, 0) = ColorRGBf(0.5f, 0.25f, 1.0f);
	const Image out = gamma(img, 2.0f);
	REQUIRE(out.colorSpace() == ColorSpace::Unspecified);
	const ColorRGBf p = out.as<ColorRGBf>()(0, 0);
	REQUIRE(p.r == Approx(0.25f));	 // 0.5^2
	REQUIRE(p.g == Approx(0.0625f)); // 0.25^2
}

TEST_CASE("clamp bounds every channel including alpha", "[ops]")
{
	Image img(1, 1, PixelFormat::RGBA32F);
	img.as<ColorRGBAf>()(0, 0) = ColorRGBAf(1.5f, -0.5f, 0.5f, 2.0f);
	const ColorRGBAf p = clamp(img).as<ColorRGBAf>()(0, 0);
	REQUIRE(p.r == Approx(1.0f));
	REQUIRE(p.g == Approx(0.0f));
	REQUIRE(p.b == Approx(0.5f));
	REQUIRE(p.a == Approx(1.0f)); // alpha clamped too
}

// --- filters -----------------------------------------------------------------

TEST_CASE("gaussianKernel is normalized and symmetric", "[ops]")
{
	const Kernel k = gaussianKernel(1, 1.0f);
	REQUIRE(k.size() == 3);
	float sum = 0.0f;
	for (const float w : k.weights)
		sum += w;
	REQUIRE(sum == Approx(1.0f));
	REQUIRE(k.at(-1, 0) == Approx(k.at(1, 0))); // symmetric
	REQUIRE(k.at(0, 0) > k.at(1, 0));			// center heaviest
}

TEST_CASE("convolve with an identity kernel is a no-op", "[ops]")
{
	Image img(2, 1, PixelFormat::RGB8, ColorSpace::Linear);
	img.as<ColorRGB8>()(0, 0) = ColorRGB8(100, 150, 200);
	img.as<ColorRGB8>()(1, 0) = ColorRGB8(10, 20, 30);
	const Kernel identity{0, {1.0f}};
	const Image out = convolve(img, identity);
	REQUIRE(out.as<ColorRGB8>()(0, 0).r == 100);
	REQUIRE(out.as<ColorRGB8>()(1, 0).b == 30);
}

TEST_CASE("convolve box-blurs toward the neighbour average", "[ops]")
{
	// A 3x1 row [0, 255, 0]; a 1x3 box blur pulls the center toward the mean.
	Image img(3, 1, PixelFormat::Gray8, ColorSpace::Linear);
	img.as<ColorGray8>()(0, 0) = ColorGray8(0);
	img.as<ColorGray8>()(1, 0) = ColorGray8(255);
	img.as<ColorGray8>()(2, 0) = ColorGray8(0);
	const Kernel box{1, {0.0f, 0.0f, 0.0f, 1.0f / 3, 1.0f / 3, 1.0f / 3, 0.0f, 0.0f, 0.0f}};
	const Image out = convolve(img, box);
	REQUIRE(out.as<ColorGray8>()(1, 0)[0] == 85); // 255/3
}

TEST_CASE("sharpen leaves a flat region unchanged", "[ops]")
{
	Image img(3, 3, PixelFormat::Gray8, ColorSpace::Linear);
	for (ColorGray8& p : img.as<ColorGray8>())
		p = ColorGray8(128);
	REQUIRE(sharpen(img).as<ColorGray8>()(1, 1)[0] == 128); // unsharp kernel sums to 1
}

// --- resize ------------------------------------------------------------------

TEST_CASE("resize nearest upsamples by replication", "[ops]")
{
	Image img(1, 1, PixelFormat::RGB8, ColorSpace::Linear);
	img.as<ColorRGB8>()(0, 0) = ColorRGB8(100, 150, 200);
	const Image up = resize(img, lain::math::Vec2i{2, 2}, Interpolation::Nearest);
	REQUIRE(up.width() == 2);
	REQUIRE(up.height() == 2);
	REQUIRE(up.as<ColorRGB8>()(1, 1).g == 150);
}

TEST_CASE("resize changes the extent", "[ops]")
{
	Image img(4, 4, PixelFormat::RGB8, ColorSpace::Linear);
	const Image down = resize(img, lain::math::Vec2i{2, 2});
	REQUIRE(down.extent() == lain::math::Vec2i{2, 2});
}

// --- geometry ----------------------------------------------------------------

TEST_CASE("crop extracts a sub-region", "[ops]")
{
	Image img(4, 4, PixelFormat::Gray8);
	auto v = img.as<ColorGray8>();
	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
			v(x, y) = ColorGray8(static_cast<std::uint8_t>(y * 4 + x));
	}
	const Image c = crop(img, 1, 1, 2, 2);
	REQUIRE(c.width() == 2);
	REQUIRE(c.height() == 2);
	REQUIRE(c.as<ColorGray8>()(0, 0)[0] == 5);	// src (1,1) = 1*4+1
	REQUIRE(c.as<ColorGray8>()(1, 1)[0] == 10); // src (2,2) = 2*4+2
}

TEST_CASE("rotate90 turns clockwise and swaps the extent", "[ops]")
{
	Image img(2, 1, PixelFormat::Gray8);
	img.as<ColorGray8>()(0, 0) = ColorGray8(7);
	img.as<ColorGray8>()(1, 0) = ColorGray8(9);
	const Image r = rotate90(img, 1);
	REQUIRE(r.width() == 1);
	REQUIRE(r.height() == 2);
	REQUIRE(r.as<ColorGray8>()(0, 0)[0] == 7); // (0,0) stays
	REQUIRE(r.as<ColorGray8>()(0, 1)[0] == 9); // (1,0) -> (0,1)
}

TEST_CASE("rotate90 of four quarter-turns is the identity", "[ops]")
{
	Image img(3, 2, PixelFormat::Gray8);
	REQUIRE(rotate90(img, 4).extent() == img.extent());
}

TEST_CASE("rotate by zero preserves extent and content", "[ops]")
{
	Image img(3, 3, PixelFormat::RGB8, ColorSpace::Linear);
	img.as<ColorRGB8>()(1, 1) = ColorRGB8(50, 100, 150);
	const Image r = rotate(img, 0.0f, Interpolation::Bilinear);
	REQUIRE(r.extent() == img.extent());
	REQUIRE(r.as<ColorRGB8>()(1, 1).g == 100);
}

// --- enforcement (release-only: the guards assert in debug) -------------------

#ifdef NDEBUG
TEST_CASE("nonlinear tone ops require Straight alpha", "[ops]")
{
	Image img(1, 1, PixelFormat::RGBA32F); // AlphaMode::Unspecified
	REQUIRE_FALSE(gamma(img, 2.0f).valid());
	REQUIRE_FALSE(contrast(img, 2.0f).valid());
}

TEST_CASE("value-blending ops require Linear", "[ops]")
{
	Image img(2, 2, PixelFormat::RGB8, ColorSpace::sRGB); // not Linear
	REQUIRE_FALSE(convolve(img, gaussianKernel(1, 1.0f)).valid());
	REQUIRE_FALSE(resize(img, lain::math::Vec2i{4, 4}).valid());
	REQUIRE_FALSE(rotate(img, 0.5f).valid());
}

TEST_CASE("crop rejects an out-of-bounds rect", "[ops]")
{
	Image img(4, 4, PixelFormat::Gray8);
	REQUIRE_FALSE(crop(img, 3, 3, 4, 4).valid());
}
#endif
