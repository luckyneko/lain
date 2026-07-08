// Unit tests for image-level conversions — format convert, sRGB<->linear, premultiply,
// and the tracked-tag transitions. Enforcement's bail path is release-only (it asserts in
// debug), so it is guarded by NDEBUG.

#include "lain/image/color.h"
#include "lain/image/convert.h"
#include "lain/image/imageview.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace lain::image;

TEST_CASE("convert changes base type and carries the tags", "[convert]")
{
	Image src(1, 1, PixelFormat::RGBA8, ColorSpace::Linear, AlphaMode::Straight);
	src.as<ColorRGBA8>()(0, 0) = ColorRGBA8(255, 0, 128, 255);

	const Image f = convert(src, PixelFormat::RGBA32F);
	REQUIRE(f.pixelFormat() == PixelFormat::RGBA32F);
	REQUIRE(f.colorSpace() == ColorSpace::Linear);
	REQUIRE(f.alphaMode() == AlphaMode::Straight);

	const ColorRGBAf p = f.as<ColorRGBAf>()(0, 0);
	REQUIRE(p.r == Approx(1.0f));
	REQUIRE(p.b == Approx(128.0f / 255.0f));
	REQUIRE(p.a == Approx(1.0f));
}

TEST_CASE("convert RGB->RGBA adds opaque Straight alpha", "[convert]")
{
	Image rgb(1, 1, PixelFormat::RGB8);
	rgb.as<ColorRGB8>()(0, 0) = ColorRGB8(10, 20, 30);

	const Image rgba = convert(rgb, PixelFormat::RGBA8);
	REQUIRE(rgba.alphaMode() == AlphaMode::Straight);
	const ColorRGBA8 p = rgba.as<ColorRGBA8>()(0, 0);
	REQUIRE(p.r == 10);
	REQUIRE(p.a == 255);
}

TEST_CASE("convert RGB->Gray uses luminance (in linear space)", "[convert]")
{
	Image rgb(1, 1, PixelFormat::RGB8, ColorSpace::Linear);
	rgb.as<ColorRGB8>()(0, 0) = ColorRGB8(255, 255, 255);

	const Image gray = convert(rgb, PixelFormat::Gray8);
	REQUIRE(gray.valid());
	REQUIRE(gray.pixelFormat() == PixelFormat::Gray8);
	REQUIRE(gray.as<ColorGray8>()(0, 0)[0] == 255); // white -> full luminance
}

TEST_CASE("convert GrayAlpha<->RGBA maps luminance and carries alpha", "[convert]")
{
	// GrayAlpha -> RGBA replicates the gray channel to RGB and keeps alpha.
	Image ga(1, 1, PixelFormat::GrayAlpha8, ColorSpace::Linear, AlphaMode::Straight);
	ga.as<ColorGrayAlpha8>()(0, 0) = ColorGrayAlpha8(128, 200);

	const Image rgba = convert(ga, PixelFormat::RGBA8);
	REQUIRE(rgba.pixelFormat() == PixelFormat::RGBA8);
	const ColorRGBA8 p = rgba.as<ColorRGBA8>()(0, 0);
	REQUIRE(p.r == 128);
	REQUIRE(p.g == 128);
	REQUIRE(p.b == 128);
	REQUIRE(p.a == 200);

	// RGB -> GrayAlpha computes luminance (linear-only) and an opaque Straight alpha.
	Image rgb(1, 1, PixelFormat::RGB8, ColorSpace::Linear);
	rgb.as<ColorRGB8>()(0, 0) = ColorRGB8(255, 255, 255);

	const Image out = convert(rgb, PixelFormat::GrayAlpha8);
	REQUIRE(out.pixelFormat() == PixelFormat::GrayAlpha8);
	REQUIRE(out.alphaMode() == AlphaMode::Straight);
	const ColorGrayAlpha8 g = out.as<ColorGrayAlpha8>()(0, 0);
	REQUIRE(g[0] == 255); // white -> full luminance
	REQUIRE(g[1] == 255); // opaque
}

TEST_CASE("convert between color spaces round-trips and flips the tag", "[convert]")
{
	Image s(1, 1, PixelFormat::RGB32F, ColorSpace::sRGB);
	s.as<ColorRGBf>()(0, 0) = ColorRGBf(0.5f, 0.5f, 0.5f);

	const Image lin = convert(s, ColorSpace::Linear);
	REQUIRE(lin.colorSpace() == ColorSpace::Linear);
	REQUIRE(lin.as<ColorRGBf>()(0, 0).r == Approx(0.2140f).margin(0.001)); // sRGB 0.5 -> ~0.214

	const Image back = convert(lin, ColorSpace::sRGB);
	REQUIRE(back.colorSpace() == ColorSpace::sRGB);
	REQUIRE(back.as<ColorRGBf>()(0, 0).r == Approx(0.5f).margin(0.001));
}

TEST_CASE("convert to the same color space is a no-op", "[convert]")
{
	Image s(1, 1, PixelFormat::RGB32F, ColorSpace::Linear);
	s.as<ColorRGBf>()(0, 0) = ColorRGBf(0.5f, 0.5f, 0.5f);
	const Image same = convert(s, ColorSpace::Linear);
	REQUIRE(same.colorSpace() == ColorSpace::Linear);
	REQUIRE(same.as<ColorRGBf>()(0, 0).r == Approx(0.5f)); // unchanged, no transfer applied
}

TEST_CASE("convert between alpha modes round-trips and flips the tag", "[convert]")
{
	Image img(1, 1, PixelFormat::RGBA32F, ColorSpace::Linear, AlphaMode::Straight);
	img.as<ColorRGBAf>()(0, 0) = ColorRGBAf(1.0f, 0.5f, 0.0f, 0.5f);

	const Image pm = convert(img, AlphaMode::Premultiplied);
	REQUIRE(pm.alphaMode() == AlphaMode::Premultiplied);
	const ColorRGBAf p = pm.as<ColorRGBAf>()(0, 0);
	REQUIRE(p.r == Approx(0.5f));  // 1.0 * 0.5
	REQUIRE(p.g == Approx(0.25f)); // 0.5 * 0.5
	REQUIRE(p.a == Approx(0.5f));  // alpha unchanged

	const Image un = convert(pm, AlphaMode::Straight);
	REQUIRE(un.alphaMode() == AlphaMode::Straight);
	REQUIRE(un.as<ColorRGBAf>()(0, 0).r == Approx(1.0f));
	REQUIRE(un.as<ColorRGBAf>()(0, 0).g == Approx(0.5f));
}

#ifdef NDEBUG
TEST_CASE("convert to Gray on a non-linear image bails (release)", "[convert]")
{
	Image rgb(1, 1, PixelFormat::RGB8, ColorSpace::sRGB); // not Linear
	rgb.as<ColorRGB8>()(0, 0) = ColorRGB8(255, 255, 255);
	const Image gray = convert(rgb, PixelFormat::Gray8);
	REQUIRE_FALSE(gray.valid());
}
#endif
