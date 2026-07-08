// Tests for the TIFF codec plugin — decodes real (tiny, embedded) uncompressed TIFFs
// through the production path: tiff::registerCodec() then io::image::decode("tiff", bytes).
// Covers RGBA8 (pixels + alpha from ExtraSamples), the 16-bit RGB U16 round-trip, grayscale
// preservation, the Unspecified color space, and rejection of non-TIFF bytes.

#include "tifffixtures.h"

#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/tiff/register.h>
#include <lain/memory/buffer.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

using lain::image::AlphaMode;
using lain::image::ColorSpace;
using lain::image::PixelFormat;

static lain::memory::Buffer bufferFrom(const unsigned char* data, std::size_t size)
{
	lain::memory::Buffer buffer(size);
	std::memcpy(buffer.data(), data, size);
	return buffer;
}

TEST_CASE("TiffReader decodes an 8-bit RGBA image with pixels and straight alpha", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	const auto image = lain::io::image::decode("tiff", bufferFrom(kTiffRgba8_2x2, sizeof(kTiffRgba8_2x2)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->height() == 2);
	REQUIRE(image->pixelFormat() == PixelFormat::RGBA8);
	REQUIRE(image->colorSpace() == ColorSpace::Unspecified); // TIFF carries no sRGB flag lain reads
	REQUIRE(image->alphaMode() == AlphaMode::Straight);		 // ExtraSamples = unassociated

	const std::uint8_t* px = image->data();
	REQUIRE(px[0] == 255); // TL red
	REQUIRE(px[3] == 255);
	REQUIRE(px[4] == 0); // TR green, alpha 128
	REQUIRE(px[5] == 255);
	REQUIRE(px[7] == 128);
	REQUIRE(px[10] == 255); // BL blue
}

TEST_CASE("TiffReader decodes a 16-bit RGB image preserving U16 samples", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	const auto image = lain::io::image::decode("tiff", bufferFrom(kTiffRgb16_2x1, sizeof(kTiffRgb16_2x1)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->pixelFormat() == PixelFormat::RGB16);

	const auto* s = reinterpret_cast<const std::uint16_t*>(image->data());
	REQUIRE(s[0] == 0x1234);
	REQUIRE(s[1] == 0x5678);
	REQUIRE(s[2] == 0x9abc);
	REQUIRE(s[3] == 0x0001);
	REQUIRE(s[4] == 0xffff);
	REQUIRE(s[5] == 0x00ff);
}

TEST_CASE("TiffReader preserves grayscale as Gray8", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	const auto image = lain::io::image::decode("tiff", bufferFrom(kTiffGray8_2x1, sizeof(kTiffGray8_2x1)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 2);
	REQUIRE(image->pixelFormat() == PixelFormat::Gray8);

	const std::uint8_t* px = image->data();
	REQUIRE(px[0] == 0x40);
	REQUIRE(px[1] == 0xC0);
}

TEST_CASE("TiffReader reads the first page of a multi-page TIFF", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	const auto image = lain::io::image::decode("tiff", bufferFrom(kTiffMultipage_2page, sizeof(kTiffMultipage_2page)));
	REQUIRE(image.has_value());
	REQUIRE(image->pixelFormat() == PixelFormat::Gray8);
	const std::uint8_t* px = image->data();
	REQUIRE(px[0] == 0x11); // page 0's pixels, not page 1's (0x33/0x44)
	REQUIRE(px[1] == 0x22);
}

TEST_CASE("TiffReader rejects non-TIFF bytes", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	const unsigned char garbage[] = {'n', 'o', 't', ' ', 'a', ' ', 't', 'i', 'f', 'f'};
	REQUIRE_FALSE(lain::io::image::decode("tiff", bufferFrom(garbage, sizeof(garbage))).has_value());
}
