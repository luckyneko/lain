// Tests for the JPEG codec plugin — decodes small JPEGs (made with sips; JPEG is lossy so
// they can't be hand-built) through the production path: jpeg::registerCodec() then
// io::image::decode("jpg", bytes). JPEG is lossy, so colour assertions use a tolerance and
// sample solid-region centres; format/space/dimension assertions are exact.

#include "jpegfixtures.h"

#include <lain/image/image.h>
#include <lain/io/image/jpeg/register.h>
#include <lain/io/image/load.h>
#include <lain/memory/buffer.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using lain::image::ColorSpace;
using lain::image::PixelFormat;

static lain::memory::Buffer bufferFrom(const unsigned char* data, std::size_t size)
{
	lain::memory::Buffer buffer(size);
	std::memcpy(buffer.data(), data, size);
	return buffer;
}

// |a - b| for two byte values (JPEG round-trip tolerance).
static bool near(int a, int b, int tol = 10) { return std::abs(a - b) <= tol; }

TEST_CASE("JpegReader decodes an RGB JPEG to RGB8/sRGB with the right colours", "[io-image-jpeg]")
{
	lain::io::image::jpeg::registerCodec();
	const auto image = lain::io::image::decode("jpg", bufferFrom(kJpegQuad16_rgb, sizeof(kJpegQuad16_rgb)));
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 16);
	REQUIRE(image->height() == 16);
	REQUIRE(image->pixelFormat() == PixelFormat::RGB8);
	REQUIRE(image->colorSpace() == ColorSpace::sRGB); // JFIF default

	// Sample each quadrant's centre (mid-block, away from JPEG edge ringing).
	const std::uint8_t* d = image->data();
	const auto at = [&](int x, int y)
	{ return d + (static_cast<std::size_t>(y) * 16 + x) * 3; };
	const std::uint8_t* tl = at(4, 4);
	REQUIRE(near(tl[0], 255));
	REQUIRE(near(tl[1], 0));
	REQUIRE(near(tl[2], 0)); // red
	const std::uint8_t* br = at(12, 12);
	REQUIRE(near(br[0], 255));
	REQUIRE(near(br[1], 255));
	REQUIRE(near(br[2], 255)); // white
}

TEST_CASE("JpegReader decodes a grayscale JPEG to Gray8", "[io-image-jpeg]")
{
	lain::io::image::jpeg::registerCodec();
	const auto image = lain::io::image::decode("jpeg", bufferFrom(kJpegGray16, sizeof(kJpegGray16)));
	REQUIRE(image.has_value());
	REQUIRE(image->pixelFormat() == PixelFormat::Gray8);
	REQUIRE(image->colorSpace() == ColorSpace::sRGB);

	const std::uint8_t* d = image->data();
	REQUIRE(near(d[16 * 8 + 4], 0x30));	 // left, dark
	REQUIRE(near(d[16 * 8 + 12], 0xC0)); // right, light
}

TEST_CASE("JpegReader rejects non-JPEG bytes", "[io-image-jpeg]")
{
	lain::io::image::jpeg::registerCodec();
	const unsigned char garbage[] = {'n', 'o', 't', ' ', 'j', 'p', 'e', 'g'};
	REQUIRE_FALSE(lain::io::image::decode("jpg", bufferFrom(garbage, sizeof(garbage))).has_value());
}
