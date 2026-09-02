// Unit tests for the color-value algorithms — convert, luminance, saturate, and the
// ColorSpace transfer curves.

#include "lain/image/colormath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace lain::image;

TEST_CASE("convert normalizes across base types", "[colormath]")
{
	const ColorRGBA8 c(255, 0, 128, 255);
	const ColorRGBAf f = convert<ColorRGBAf>(c);
	REQUIRE(f.r == Approx(1.0f));
	REQUIRE(f.g == Approx(0.0f));
	REQUIRE(f.b == Approx(128.0f / 255.0f));
	REQUIRE(f.a == Approx(1.0f));

	// Round-trip back to u8 is lossless at these values.
	const ColorRGBA8 back = convert<ColorRGBA8>(f);
	REQUIRE(back.r == 255);
	REQUIRE(back.g == 0);
	REQUIRE(back.b == 128);
	REQUIRE(back.a == 255);
}

TEST_CASE("convert maps channel counts through canonical RGBA", "[colormath]")
{
	// RGB -> RGBA gains opaque alpha.
	const ColorRGBA8 rgba = convert<ColorRGBA8>(ColorRGB8(255, 0, 0));
	REQUIRE(rgba.r == 255);
	REQUIRE(rgba.g == 0);
	REQUIRE(rgba.a == 255);

	// RGBA -> RGB drops alpha.
	const ColorRGB8 rgb = convert<ColorRGB8>(ColorRGBA8(10, 20, 30, 40));
	REQUIRE(rgb.r == 10);
	REQUIRE(rgb.g == 20);
	REQUIRE(rgb.b == 30);

	// Gray -> RGB replicates the single channel.
	const ColorRGB8 grayToRgb = convert<ColorRGB8>(ColorGray8(128));
	REQUIRE(grayToRgb.r == 128);
	REQUIRE(grayToRgb.g == 128);
	REQUIRE(grayToRgb.b == 128);

	// White RGB -> Gray is full luminance (weights sum to 1).
	const ColorGray8 white = convert<ColorGray8>(ColorRGB8(255, 255, 255));
	REQUIRE(white[0] == 255);
	// Pure green carries most of the Rec709 luminance.
	const ColorGrayf green = convert<ColorGrayf>(ColorRGBf(0.0f, 1.0f, 0.0f));
	REQUIRE(green[0] == Approx(0.7152f));
}

TEST_CASE("convert to the same type is an exact identity", "[colormath]")
{
	const ColorRGBA8 c(1, 2, 3, 4);
	const ColorRGBA8 same = convert<ColorRGBA8>(c);
	REQUIRE(same.r == 1);
	REQUIRE(same.g == 2);
	REQUIRE(same.b == 3);
	REQUIRE(same.a == 4);
}

TEST_CASE("luminance is Rec709 over unit RGB", "[colormath]")
{
	REQUIRE(luminance(ColorRGBf(1.0f, 1.0f, 1.0f)) == Approx(1.0f));
	REQUIRE(luminance(ColorRGBf(0.0f, 0.0f, 0.0f)) == Approx(0.0f));
	REQUIRE(luminance(ColorRGBAf(1.0f, 0.0f, 0.0f, 0.3f)) == Approx(0.2126f)); // alpha ignored
	REQUIRE(luminance(ColorGrayf(0.5f)) == Approx(0.5f));
}

TEST_CASE("saturate clamps float channels to [0,1]", "[colormath]")
{
	const ColorRGBAf c = saturate(ColorRGBAf(1.5f, -0.5f, 0.5f, 2.0f));
	REQUIRE(c.r == Approx(1.0f));
	REQUIRE(c.g == Approx(0.0f));
	REQUIRE(c.b == Approx(0.5f));
	REQUIRE(c.a == Approx(1.0f));
}

TEST_CASE("convert maps HSV to RGB", "[colormath]")
{
	// Primary/secondary hues at full saturation & value.
	const ColorRGBf red = convert(ColorHSVf{0.0f, 1.0f, 1.0f});
	REQUIRE(red.r == Approx(1.0f));
	REQUIRE(red.g == Approx(0.0f));
	REQUIRE(red.b == Approx(0.0f));

	const ColorRGBf green = convert(ColorHSVf{120.0f, 1.0f, 1.0f});
	REQUIRE(green.r == Approx(0.0f));
	REQUIRE(green.g == Approx(1.0f));
	REQUIRE(green.b == Approx(0.0f));

	const ColorRGBf blue = convert(ColorHSVf{240.0f, 1.0f, 1.0f});
	REQUIRE(blue.b == Approx(1.0f));
	REQUIRE(blue.r == Approx(0.0f));

	// Saturation 0 -> greyscale at `value`, regardless of hue; hue wraps mod 360.
	const ColorRGBf grey = convert(ColorHSVf{200.0f, 0.0f, 0.5f});
	REQUIRE(grey.r == Approx(0.5f));
	REQUIRE(grey.g == Approx(0.5f));
	REQUIRE(grey.b == Approx(0.5f));
	REQUIRE(convert(ColorHSVf{360.0f, 1.0f, 1.0f}).r == Approx(1.0f)); // 360 wraps to red

	// Dst override lands directly in 0-255 RGBA bytes (alpha opaque).
	const ColorRGBA8 red8 = convert<ColorRGBA8>(ColorHSVf{0.0f, 1.0f, 1.0f});
	REQUIRE(int(red8.r) == 255);
	REQUIRE(int(red8.g) == 0);
	REQUIRE(int(red8.a) == 255);
}

TEST_CASE("toLinear and fromLinear are inverses of each other", "[colormath]")
{
	// The whole compose-through-Linear scheme rests on this: if a space's pair did not invert,
	// converting A -> B -> A would drift instead of returning the value it started with.
	for (const ColorSpace space : {ColorSpace::sRGB, ColorSpace::BT709})
	{
		for (float v = 0.0f; v <= 1.0f; v += 0.05f)
		{
			REQUIRE(fromLinear(space, toLinear(space, v)) == Approx(v).margin(1e-5));
			REQUIRE(toLinear(space, fromLinear(space, v)) == Approx(v).margin(1e-5));
		}
	}
}

TEST_CASE("Linear and Unspecified pass values through unchanged", "[colormath]")
{
	// Linear MUST be the identity — it is the intermediate every other pairing composes
	// through. Unspecified is the documented total-function fallback, not a meaning.
	for (const ColorSpace space : {ColorSpace::Linear, ColorSpace::Unspecified})
	{
		REQUIRE(toLinear(space, 0.25f) == 0.25f);
		REQUIRE(fromLinear(space, 0.25f) == 0.25f);
	}
}

TEST_CASE("the BT709 curve is the Rec.709 OETF, not sRGB's", "[colormath]")
{
	// The linear toe below the breakpoint is an exact divide by 4.5.
	REQUIRE(toLinear(ColorSpace::BT709, 0.045f) == Approx(0.01f));
	REQUIRE(fromLinear(ColorSpace::BT709, 0.01f) == Approx(0.045f));

	// Mid-grey: the two spaces differ by ~20% of the linear value. That gap is the whole
	// reason BT709 is its own enumerator rather than footage being tagged sRGB.
	REQUIRE(toLinear(ColorSpace::BT709, 0.5f) == Approx(0.2596f).margin(0.001));
	REQUIRE(toLinear(ColorSpace::sRGB, 0.5f) == Approx(0.2140f).margin(0.001));
}

TEST_CASE("the BT709 breakpoint steps by the spec's rounding", "[colormath]")
{
	// Rec.709's published constants leave the two segments meeting at ~2e-4 rather than
	// exactly. Pinned so the discontinuity is a recorded property, not a surprise, and so
	// "fixing" it to the continuous alpha/beta values shows up as a deliberate change.
	const float below = fromLinear(ColorSpace::BT709, 0.018f - 1e-6f);
	const float above = fromLinear(ColorSpace::BT709, 0.018f + 1e-6f);
	REQUIRE(below == Approx(0.081f).margin(1e-4));
	REQUIRE(above == Approx(0.081f).margin(1e-3));
	REQUIRE(above - below < 1e-3f);
}
