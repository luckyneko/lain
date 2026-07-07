// Unit tests for the Color type — GLM overlay + the format bridge.

#include "lain/image/color.h"

#include <catch2/catch_test_macros.hpp>

using namespace lain::image;

TEST_CASE("Color inherits GLM channel access and arithmetic", "[color]")
{
	ColorRGBA8 c(10, 20, 30, 40);
	REQUIRE(c[0] == 10);
	REQUIRE(c.r == 10); // .rgba accessors from GLM
	REQUIRE(c.g == 20);
	REQUIRE(c.b == 30);
	REQUIRE(c.a == 40);

	// GLM operators return the base Vec; the Base-converting ctor re-wraps into a Color.
	ColorRGBA8 sum = c + c;
	REQUIRE(sum.r == 20);
	REQUIRE(sum.a == 80);
}

TEST_CASE("each Color typedef carries its PixelFormat", "[color]")
{
	STATIC_REQUIRE(ColorGray8::format == PixelFormat::Gray8);
	STATIC_REQUIRE(ColorRGB16::format == PixelFormat::RGB16);
	STATIC_REQUIRE(ColorRGBA8::format == PixelFormat::RGBA8);
	STATIC_REQUIRE(ColorRGBAf::format == PixelFormat::RGBA32F);
}
