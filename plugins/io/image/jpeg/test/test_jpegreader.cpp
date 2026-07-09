// Tests for the JPEG codec plugin — decodes small JPEGs (made with sips; JPEG is lossy so
// they can't be hand-built) through the production path: jpeg::registerCodec() then
// io::image::decode("jpg", bytes). JPEG is lossy, so colour assertions use a tolerance and
// sample solid-region centres; format/space/dimension assertions are exact.

#include "jpegfixtures.h"

#include <lain/image/image.h>
#include <lain/io/image/jpeg/register.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
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

// A solid-colour image — JPEG is lossy, but a flat colour round-trips within a small
// tolerance (no block-edge ringing to worry about).
static lain::image::Image solid(int w, int h, PixelFormat format, std::uint8_t value)
{
	lain::image::Image image(w, h, format, ColorSpace::sRGB);
	std::uint8_t* px = image.data();
	for (std::size_t i = 0; i < image.byteSize(); ++i)
		px[i] = value;
	return image;
}

TEST_CASE("JpegWriter round-trips an RGB8 image (lossy, within tolerance)", "[io-image-jpeg]")
{
	lain::io::image::jpeg::registerCodec(); // registers the reader + writer
	const lain::image::Image original = solid(16, 16, PixelFormat::RGB8, 137);

	const auto encoded = lain::io::image::encode("jpg", original);
	REQUIRE(encoded.has_value());

	const auto decoded = lain::io::image::decode("jpg", *encoded);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->pixelFormat() == PixelFormat::RGB8);
	const std::uint8_t* px = decoded->data();
	const std::size_t centre = (8 * 16 + 8) * 3;
	REQUIRE(near(px[centre + 0], 137));
	REQUIRE(near(px[centre + 1], 137));
	REQUIRE(near(px[centre + 2], 137));
}

TEST_CASE("JpegWriter rejects an alpha-bearing image rather than dropping alpha", "[io-image-jpeg]")
{
	// JPEG has no alpha; the writer must NOT silently discard it — encode fails loudly, and
	// the caller drops alpha explicitly (convert to RGB8) if that's what they want.
	lain::io::image::jpeg::registerCodec();
	REQUIRE_FALSE(lain::io::image::encode("jpg", solid(16, 16, PixelFormat::RGBA8, 90)).has_value());

	// The explicit form works: an RGB8 (alpha already dropped by the caller) encodes.
	REQUIRE(lain::io::image::encode("jpg", solid(16, 16, PixelFormat::RGB8, 90)).has_value());
}

TEST_CASE("JpegWriter rejects a 16-bit image (JPEG is 8-bit)", "[io-image-jpeg]")
{
	lain::io::image::jpeg::registerCodec();
	REQUIRE_FALSE(lain::io::image::encode("jpg", solid(4, 4, PixelFormat::RGB16, 1)).has_value());
}
