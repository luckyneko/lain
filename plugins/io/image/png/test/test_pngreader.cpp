// Tests for the PNG codec plugin — decodes real (tiny, embedded) PNGs through the
// production path: png::registerCodec() then io::image::decode("png", bytes). Covers the
// formats lain represents natively and preserves (RGBA8, RGB16 with its U16 round-trip,
// Gray8, GrayAlpha8), the honest ColorSpace detection (sRGB chunk vs untagged), and
// rejection of non-PNG bytes.

#include "pngfixtures.h"

#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/png/register.h>
#include <lain/io/image/save.h>
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

// A test image with a recognisable per-byte ramp, for the writer round-trips below.
static lain::image::Image rampImage(int w, int h, PixelFormat format)
{
	lain::image::Image image(w, h, format, ColorSpace::sRGB, AlphaMode::Straight);
	std::uint8_t* px = image.data();
	for (std::size_t i = 0; i < image.byteSize(); ++i)
		px[i] = static_cast<std::uint8_t>(i * 7 + 1);
	return image;
}

TEST_CASE("PngWriter round-trips an 8-bit RGBA image losslessly", "[io-image-png]")
{
	lain::io::image::png::registerCodec(); // registers the reader + writer
	const lain::image::Image original = rampImage(3, 2, PixelFormat::RGBA8);

	const auto encoded = lain::io::image::encode("png", original);
	REQUIRE(encoded.has_value());

	const auto decoded = lain::io::image::decode("png", *encoded);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->width() == 3);
	REQUIRE(decoded->height() == 2);
	REQUIRE(decoded->pixelFormat() == PixelFormat::RGBA8);
	for (std::size_t i = 0; i < original.byteSize(); ++i)
		REQUIRE(decoded->data()[i] == original.data()[i]); // PNG is lossless
}

TEST_CASE("PngWriter round-trips a 16-bit RGB image (U16 write + swap)", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	const lain::image::Image original = rampImage(2, 2, PixelFormat::RGB16);

	const auto encoded = lain::io::image::encode("png", original);
	REQUIRE(encoded.has_value());

	// encode must not mutate the input — the row pointers alias it, and libpng only reads
	// them on write (this is what lets us skip the pixel copy).
	for (std::size_t i = 0; i < original.byteSize(); ++i)
		REQUIRE(original.data()[i] == static_cast<std::uint8_t>(i * 7 + 1));

	const auto decoded = lain::io::image::decode("png", *encoded);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->pixelFormat() == PixelFormat::RGB16);
	const auto* out16 = reinterpret_cast<const std::uint16_t*>(decoded->data());
	const auto* in16 = reinterpret_cast<const std::uint16_t*>(original.data());
	for (std::size_t i = 0; i < original.byteSize() / 2; ++i)
		REQUIRE(out16[i] == in16[i]); // full 16-bit values survive
}

TEST_CASE("PngWriter rejects a float format PNG can't store", "[io-image-png]")
{
	lain::io::image::png::registerCodec();
	REQUIRE_FALSE(lain::io::image::encode("png", rampImage(2, 2, PixelFormat::RGB32F)).has_value());
}
