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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

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
	// Read, not assumed: this fixture's Exif carries ColorSpace (0xA001) = 1, the field the
	// Exif specification defines to mean sRGB. Before ADR-0020 every JPEG got this answer
	// whatever it said; now it is what the file states.
	REQUIRE(image->colorSpace() == ColorSpace::sRGB);

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

	// Unspecified, and this fixture is why the rule is worth having: it carries a 4.5 KB APP2 ICC
	// profile (a grey gamma-2.2 profile, from the `sips` that generated it) and no Exif ColorSpace
	// tag. It is genuinely NOT sRGB, and before ADR-0020 lain reported it as sRGB — an ordinary
	// file, silently mislabelled, with no marker ever consulted.
	REQUIRE(image->colorSpace() == ColorSpace::Unspecified);

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

TEST_CASE("JpegWriter refuses a space it would encode and read back as sRGB", "[io-image-jpeg]")
{
	// The worst silent path in the image stack before ADR-0020: Linear pixels encoded without
	// complaint into a file whose JFIF header makes this codec's own reader call them sRGB — a tag
	// nothing wrote, over values a whole transfer curve away from it.
	lain::io::image::jpeg::registerCodec();
	lain::image::Image image = solid(8, 8, PixelFormat::RGB8, 128);

	image.setColorSpace(ColorSpace::Linear);
	REQUIRE_FALSE(lain::io::image::canEncode("jpg", image));
	REQUIRE_FALSE(lain::io::image::encode("jpg", image).has_value());

	image.setColorSpace(ColorSpace::BT709);
	REQUIRE_FALSE(lain::io::image::canEncode("jpg", image));

	image.setColorSpace(ColorSpace::sRGB);
	REQUIRE(lain::io::image::canEncode("jpg", image));
}

// --- the quality knob (M13 slice 5) ----------------------------------------------

// An image with detail to lose: a gradient carrying a small deterministic jitter, so quality has
// something to spend. A flat colour survives every quality equally, which is exactly why the
// round-trip case above uses one and this one must not.
static lain::image::Image textureImage(int w, int h)
{
	lain::image::Image image(w, h, PixelFormat::RGB8, ColorSpace::sRGB);
	std::uint8_t* px = image.data();
	for (std::size_t i = 0; i < image.byteSize(); ++i)
	{
		const std::size_t x = (i / 3) % static_cast<std::size_t>(w);
		const std::size_t y = (i / 3) / static_cast<std::size_t>(w);
		px[i] = static_cast<std::uint8_t>((x * 2 + y * 3) ^ ((i * 37) & 0x1f));
	}
	return image;
}

// How far the worst channel of a decoded image strayed from the original.
static int worstError(const lain::image::Image& original, const lain::image::Image& decoded)
{
	int worst = 0;
	for (std::size_t i = 0; i < original.byteSize(); ++i)
	{
		const int delta = static_cast<int>(decoded.data()[i]) - static_cast<int>(original.data()[i]);
		worst = std::max(worst, delta < 0 ? -delta : delta);
	}
	return worst;
}

TEST_CASE("a lower JpegWriter quality is both smaller and further from the source", "[io-image-jpeg]")
{
	// BOTH halves, and the second is the one that matters: size alone would pass just as happily
	// if the number reached some other stb parameter, since almost anything written into that call
	// changes the byte count.
	lain::io::image::jpeg::registerCodec();
	const lain::image::Image original = textureImage(32, 32);

	auto encodeAt = [&original](std::uint8_t quality)
	{
		lain::io::image::ImageWriterOptions options;
		options.quality = quality;
		const auto encoded = lain::io::image::encode("jpg", original, options);
		REQUIRE(encoded.has_value());
		const auto decoded = lain::io::image::decode("jpg", *encoded);
		REQUIRE(decoded.has_value());
		return std::pair<std::size_t, int>{encoded->size(), worstError(original, *decoded)};
	};

	const auto [lowBytes, lowError] = encodeAt(20);
	const auto [highBytes, highError] = encodeAt(95);

	CHECK(lowBytes < highBytes);
	CHECK(lowError > highError);
}

TEST_CASE("an unset quality is the 90 JpegWriter has always written", "[io-image-jpeg]")
{
	// The seam deliberately does not spell that number (options.quality is an optional, not a
	// sentinel), so this is what holds "a caller who says nothing gets the bytes it got before the
	// knob existed" to the codec rather than to a comment.
	lain::io::image::jpeg::registerCodec();
	const lain::image::Image original = textureImage(16, 16);

	lain::io::image::ImageWriterOptions ninety;
	ninety.quality = 90;

	const auto unset = lain::io::image::encode("jpg", original);
	const auto asked = lain::io::image::encode("jpg", original, ninety);
	REQUIRE(unset.has_value());
	REQUIRE(asked.has_value());
	REQUIRE(unset->size() == asked->size());
	CHECK(std::memcmp(unset->data(), asked->data(), unset->size()) == 0);
}

TEST_CASE("JpegWriter ignores Compression, which must never mean blurrier", "[io-image-jpeg]")
{
	// stb has no effort knob, and routing Small into quality would look helpful while quietly
	// making a size request into a fidelity one — the confusion the two fields exist to keep apart.
	lain::io::image::jpeg::registerCodec();
	const lain::image::Image original = textureImage(16, 16);

	lain::io::image::ImageWriterOptions small;
	small.compression = lain::io::image::Compression::Small;

	const auto plain = lain::io::image::encode("jpg", original);
	const auto squeezed = lain::io::image::encode("jpg", original, small);
	REQUIRE(plain.has_value());
	REQUIRE(squeezed.has_value());
	REQUIRE(plain->size() == squeezed->size());
	CHECK(std::memcmp(plain->data(), squeezed->data(), plain->size()) == 0);
}
