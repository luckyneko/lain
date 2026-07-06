// Unit tests for lain::image::Image. Pure std, no driver.

#include "lain/image/image.h"

#include <lain/math/types.h>

#include <catch2/catch_test_macros.hpp>

using lain::image::Format;
using lain::image::Image;

TEST_CASE("Image constructs a tightly-packed RGBA8 buffer", "[image]")
{
	const Image img(4, 2);
	REQUIRE(img.width() == 4);
	REQUIRE(img.height() == 2);
	REQUIRE(img.extent() == lain::math::Vec2i{4, 2});
	REQUIRE(img.format() == Format::RGBA8);
	REQUIRE(img.pixelCount() == 8u);
	REQUIRE(img.byteSize() == 4u * 2u * 4u);
	REQUIRE(img.bytes().size() == 32u);
	REQUIRE(img.valid());
}

TEST_CASE("a default Image is empty and invalid", "[image]")
{
	const Image img;
	REQUIRE_FALSE(img.valid());
	REQUIRE(img.byteSize() == 0u);
	REQUIRE(img.bytes().empty());
}

TEST_CASE("Image pixel bytes are writable", "[image]")
{
	Image img(2, 1);
	img.bytes()[0] = 200;
	img.bytes()[4] = 100;
	REQUIRE(img.bytes()[0] == 200);
	REQUIRE(img.bytes()[4] == 100);
}

TEST_CASE("Image::toString names extent and format", "[image]")
{
	REQUIRE(Image(64, 64).toString() == "Image 64x64 RGBA8");
	REQUIRE(Image().toString() == "Image 0x0 RGBA8");
}

TEST_CASE("bytesPerPixel is 4 for RGBA8", "[image]")
{
	REQUIRE(Image::bytesPerPixel(Format::RGBA8) == 4u);
}
