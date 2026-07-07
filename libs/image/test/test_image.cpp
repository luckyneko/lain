// Unit tests for lain::image::Image + PixelFormat descriptor. Pure std, no driver.

#include "lain/image/image.h"
#include "lain/image/pixelformat.h"

#include <lain/math/types.h>

#include <catch2/catch_test_macros.hpp>

using lain::image::AlphaMode;
using lain::image::ChannelType;
using lain::image::ColorModel;
using lain::image::ColorSpace;
using lain::image::Image;
using lain::image::PixelFormat;

TEST_CASE("Image constructs a tightly-packed RGBA8 buffer", "[image]")
{
	const Image img(4, 2);
	REQUIRE(img.width() == 4);
	REQUIRE(img.height() == 2);
	REQUIRE(img.extent() == lain::math::Vec2i{4, 2});
	REQUIRE(img.pixelFormat() == PixelFormat::RGBA8);
	REQUIRE(img.pixelCount() == 8u);
	REQUIRE(img.byteSize() == 4u * 2u * 4u);
	REQUIRE(img.byteSize() == 32u);
	REQUIRE(img.valid());
}

TEST_CASE("a fresh Image is Unspecified color space and alpha mode", "[image]")
{
	const Image img(2, 2);
	REQUIRE(img.colorSpace() == ColorSpace::Unspecified);
	REQUIRE(img.alphaMode() == AlphaMode::Unspecified);
}

TEST_CASE("color space and alpha mode are settable tags", "[image]")
{
	Image img(2, 2);
	img.setColorSpace(ColorSpace::Linear);
	img.setAlphaMode(AlphaMode::Premultiplied);
	REQUIRE(img.colorSpace() == ColorSpace::Linear);
	REQUIRE(img.alphaMode() == AlphaMode::Premultiplied);
}

TEST_CASE("a default Image is empty and invalid", "[image]")
{
	const Image img;
	REQUIRE_FALSE(img.valid());
	REQUIRE(img.byteSize() == 0u);
}

TEST_CASE("Image pixel bytes are writable", "[image]")
{
	Image img(2, 1);
	img.data()[0] = 200;
	img.data()[4] = 100;
	REQUIRE(img.data()[0] == 200);
	REQUIRE(img.data()[4] == 100);
}

TEST_CASE("Image::toString names extent and format", "[image]")
{
	REQUIRE(Image(64, 64).toString() == "Image 64x64 RGBA8");
	REQUIRE(Image().toString() == "Image 0x0 RGBA8");
}

TEST_CASE("bytesPerPixel is 4 for RGBA8", "[image]")
{
	REQUIRE(Image::bytesPerPixel(PixelFormat::RGBA8) == 4u);
}

TEST_CASE("PixelFormat descriptor reports model, channel type and sizes", "[image]")
{
	const auto rgba8 = lain::image::descriptor(PixelFormat::RGBA8);
	REQUIRE(rgba8.model == ColorModel::RGBA);
	REQUIRE(rgba8.channelType == ChannelType::U8);
	REQUIRE(rgba8.channelCount() == 4u);
	REQUIRE(rgba8.bytesPerChannel() == 1u);
	REQUIRE(rgba8.bytesPerPixel() == 4u);
	REQUIRE(rgba8.hasAlpha());

	const auto gray16 = lain::image::descriptor(PixelFormat::Gray16);
	REQUIRE(gray16.model == ColorModel::Gray);
	REQUIRE(gray16.channelType == ChannelType::U16);
	REQUIRE(gray16.channelCount() == 1u);
	REQUIRE(gray16.bytesPerPixel() == 2u);
	REQUIRE_FALSE(gray16.hasAlpha());

	const auto rgb32f = lain::image::descriptor(PixelFormat::RGB32F);
	REQUIRE(rgb32f.channelCount() == 3u);
	REQUIRE(rgb32f.bytesPerChannel() == 4u);
	REQUIRE(rgb32f.bytesPerPixel() == 12u);
	REQUIRE_FALSE(rgb32f.hasAlpha());
}

TEST_CASE("descriptor() is usable in a constexpr context", "[image]")
{
	STATIC_REQUIRE(lain::image::descriptor(PixelFormat::RGBA8).bytesPerPixel() == 4u);
	STATIC_REQUIRE(lain::image::descriptor(PixelFormat::RGB32F).bytesPerPixel() == 12u);
	STATIC_REQUIRE(lain::image::descriptor(PixelFormat::Gray8).channelCount() == 1u);
	STATIC_REQUIRE_FALSE(lain::image::descriptor(PixelFormat::RGB8).hasAlpha());
}
