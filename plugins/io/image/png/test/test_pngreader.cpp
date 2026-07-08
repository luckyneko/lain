// Tests for the PNG codec plugin — decodes real (tiny, embedded) PNGs through the
// production path: png::registerCodec() then io::image::decode("png", bytes). Covers the
// formats lain represents natively and preserves (RGBA8, RGB16 with its U16 round-trip,
// Gray8, GrayAlpha8), the honest ColorSpace detection (sRGB chunk vs untagged), and
// rejection of non-PNG bytes.

#include "pngfixtures.h"

#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/png/register.h>
#include <lain/memory/buffer.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

using lain::image::AlphaMode;
using lain::image::ColorSpace;
using lain::image::PixelFormat;

// A Buffer holding a copy of an embedded fixture's bytes — what io::read would produce.
static lain::memory::Buffer bufferFrom(const unsigned char* data, std::size_t size)
{
	lain::memory::Buffer buffer(size);
	std::memcpy(buffer.data(), data, size);
	return buffer;
}

TEST_CASE("PngReader decodes an 8-bit RGBA image with correct pixels and tags", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const auto image = lain::io::image::decode("png", bufferFrom(kPngRgba8_2x2, sizeof(kPngRgba8_2x2)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->height() == 2);
	REQUIRE(image->pixelFormat() == PixelFormat::RGBA8);
	REQUIRE(image->colorSpace() == ColorSpace::sRGB);
	REQUIRE(image->alphaMode() == AlphaMode::Straight);

	const std::uint8_t* px = image->data();
	REQUIRE(px[0] == 255); // TL red
	REQUIRE(px[1] == 0);
	REQUIRE(px[2] == 0);
	REQUIRE(px[3] == 255);
	REQUIRE(px[4] == 0); // TR green, alpha 128
	REQUIRE(px[5] == 255);
	REQUIRE(px[6] == 0);
	REQUIRE(px[7] == 128);
	REQUIRE(px[8] == 0); // BL blue
	REQUIRE(px[10] == 255);
}

TEST_CASE("PngReader decodes a 16-bit RGB image preserving U16 samples", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const auto image = lain::io::image::decode("png", bufferFrom(kPngRgb16_2x1, sizeof(kPngRgb16_2x1)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->height() == 1);
	REQUIRE(image->pixelFormat() == PixelFormat::RGB16);
	REQUIRE(image->colorSpace() == ColorSpace::sRGB);

	// Native uint16 — the reader byte-swaps PNG's big-endian samples to host order.
	const auto* s = reinterpret_cast<const std::uint16_t*>(image->data());
	REQUIRE(s[0] == 0x1234);
	REQUIRE(s[1] == 0x5678);
	REQUIRE(s[2] == 0x9abc);
	REQUIRE(s[3] == 0x0001);
	REQUIRE(s[4] == 0xffff);
	REQUIRE(s[5] == 0x00ff);
}

TEST_CASE("PngReader preserves grayscale as Gray8 (no expansion to RGB)", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const auto image = lain::io::image::decode("png", bufferFrom(kPngGray8_2x1, sizeof(kPngGray8_2x1)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->pixelFormat() == PixelFormat::Gray8);
	REQUIRE(image->colorSpace() == ColorSpace::sRGB);

	const std::uint8_t* px = image->data();
	REQUIRE(px[0] == 0x40);
	REQUIRE(px[1] == 0xC0);
}

TEST_CASE("PngReader preserves gray+alpha as GrayAlpha8", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const auto image = lain::io::image::decode("png", bufferFrom(kPngGrayAlpha8_2x1, sizeof(kPngGrayAlpha8_2x1)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->pixelFormat() == PixelFormat::GrayAlpha8);
	REQUIRE(image->alphaMode() == AlphaMode::Straight);

	const std::uint8_t* px = image->data();
	REQUIRE(px[0] == 0x40); // gray, alpha
	REQUIRE(px[1] == 0xff);
	REQUIRE(px[2] == 0xC0);
	REQUIRE(px[3] == 0x80);
}

TEST_CASE("PngReader reports Unspecified color space for an untagged PNG", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const auto image = lain::io::image::decode("png", bufferFrom(kPngUntaggedRgb8_1x1, sizeof(kPngUntaggedRgb8_1x1)));
	REQUIRE(image.has_value());
	REQUIRE(image->pixelFormat() == PixelFormat::RGB8);
	REQUIRE(image->colorSpace() == ColorSpace::Unspecified); // no colour chunk -> honestly unknown
}

TEST_CASE("PngReader rejects non-PNG bytes", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const unsigned char garbage[] = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
	REQUIRE_FALSE(lain::io::image::decode("png", bufferFrom(garbage, sizeof(garbage))).has_value());
}
