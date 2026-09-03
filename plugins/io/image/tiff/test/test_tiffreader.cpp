// Tests for the TIFF codec plugin — decodes real (tiny, embedded) uncompressed TIFFs
// through the production path: tiff::registerCodec() then io::image::decode("tiff", bytes).
// Covers RGBA8 (pixels + alpha from ExtraSamples), the 16-bit RGB U16 round-trip, grayscale
// preservation, the Unspecified color space, and rejection of non-TIFF bytes.

#include "tiffcolor.h" // transferTable — so the test locates the real curve, not a guessed offset
#include "tifffixtures.h"

#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/io/image/tiff/register.h>
#include <lain/memory/buffer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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
	REQUIRE(image->colorSpace() == ColorSpace::Unspecified); // no ICC profile, no TransferFunction
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

// A test image with a recognisable per-byte ramp, for the writer round-trips below.
static lain::image::Image rampImage(int w, int h, PixelFormat format, AlphaMode alpha = AlphaMode::Unspecified)
{
	lain::image::Image image(w, h, format, ColorSpace::Unspecified, alpha);
	std::uint8_t* px = image.data();
	for (std::size_t i = 0; i < image.byteSize(); ++i)
		px[i] = static_cast<std::uint8_t>(i * 5 + 3);
	return image;
}

TEST_CASE("TiffWriter round-trips an 8-bit RGBA image losslessly (LZW)", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec(); // registers the reader + writer
	const lain::image::Image original = rampImage(3, 2, PixelFormat::RGBA8, AlphaMode::Straight);

	const auto encoded = lain::io::image::encode("tiff", original);
	REQUIRE(encoded.has_value());

	const auto decoded = lain::io::image::decode("tiff", *encoded);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->width() == 3);
	REQUIRE(decoded->height() == 2);
	REQUIRE(decoded->pixelFormat() == PixelFormat::RGBA8);
	REQUIRE(decoded->alphaMode() == AlphaMode::Straight); // carried via ExtraSamples
	for (std::size_t i = 0; i < original.byteSize(); ++i)
		REQUIRE(decoded->data()[i] == original.data()[i]); // LZW is lossless
}

TEST_CASE("TiffWriter round-trips a 16-bit RGB image (U16, libtiff byte order)", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	const lain::image::Image original = rampImage(2, 2, PixelFormat::RGB16);

	const auto encoded = lain::io::image::encode("tiff", original);
	REQUIRE(encoded.has_value());

	const auto decoded = lain::io::image::decode("tiff", *encoded);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->pixelFormat() == PixelFormat::RGB16);
	const auto* out16 = reinterpret_cast<const std::uint16_t*>(decoded->data());
	const auto* in16 = reinterpret_cast<const std::uint16_t*>(original.data());
	for (std::size_t i = 0; i < original.byteSize() / 2; ++i)
		REQUIRE(out16[i] == in16[i]); // full 16-bit values survive
}

TEST_CASE("TiffWriter rejects a float format this writer won't store", "[io-image-tiff]")
{
	lain::io::image::tiff::registerCodec();
	REQUIRE_FALSE(lain::io::image::encode("tiff", rampImage(2, 2, PixelFormat::RGB32F)).has_value());
}

// --- the colour tags, both directions (ADR-0020) ---------------------------------

TEST_CASE("TiffWriter records the ColorSpace as a TransferFunction, for every space", "[io-image-tiff]")
{
	// TIFF is the one still format here that round-trips all four values. That is not a nicety:
	// PNG must refuse BT709 and JPEG cannot state anything but sRGB, so this is where a caller
	// who needs the tag preserved is sent.
	lain::io::image::tiff::registerCodec();

	for (const ColorSpace space :
		 {ColorSpace::sRGB, ColorSpace::Linear, ColorSpace::BT709, ColorSpace::Unspecified})
	{
		for (const PixelFormat format : {PixelFormat::RGB8, PixelFormat::Gray8, PixelFormat::RGB16})
		{
			lain::image::Image original = rampImage(3, 2, format);
			original.setColorSpace(space);

			const auto encoded = lain::io::image::encode("tiff", original);
			REQUIRE(encoded.has_value());
			const auto decoded = lain::io::image::decode("tiff", *encoded);
			REQUIRE(decoded.has_value());
			REQUIRE(decoded->colorSpace() == space);

			for (std::size_t i = 0; i < original.byteSize(); ++i)
				REQUIRE(decoded->data()[i] == original.data()[i]); // recording a tag moves no pixels
		}
	}
}

TEST_CASE("sRGB and BT709 are told apart, which is the whole reason TIFF states a curve", "[io-image-tiff]")
{
	// The two spaces share primaries and differ only in transfer, so they are byte-identical on
	// disk. A format that cannot distinguish them (PNG, via gAMA) has to refuse one; TIFF states
	// the curve itself, so the round trip keeps them separate.
	lain::io::image::tiff::registerCodec();

	lain::image::Image srgb = rampImage(2, 2, PixelFormat::RGB8);
	srgb.setColorSpace(ColorSpace::sRGB);
	lain::image::Image bt709 = rampImage(2, 2, PixelFormat::RGB8);
	bt709.setColorSpace(ColorSpace::BT709);

	const auto a = lain::io::image::encode("tiff", srgb);
	const auto b = lain::io::image::encode("tiff", bt709);
	REQUIRE(a.has_value());
	REQUIRE(b.has_value());
	REQUIRE(lain::io::image::decode("tiff", *a)->colorSpace() == ColorSpace::sRGB);
	REQUIRE(lain::io::image::decode("tiff", *b)->colorSpace() == ColorSpace::BT709);
}

TEST_CASE("a TransferFunction lain did not write reads as Unspecified", "[io-image-tiff]")
{
	// The comparison is exact rather than a tolerance window, so a curve that is merely CLOSE to
	// one lain knows is not claimed as it. That is what keeps this a fact instead of a second
	// gAMA-style heuristic — the price being that a foreign file states its curve in vain.
	lain::io::image::tiff::registerCodec();
	lain::image::Image image = rampImage(2, 2, PixelFormat::RGB8);
	image.setColorSpace(ColorSpace::sRGB);
	const auto encoded = lain::io::image::encode("tiff", image);
	REQUIRE(encoded.has_value());

	// Perturb one entry of the stored table by a single count. Searching for the table's bytes is
	// what keeps this test honest about WHERE the tag is: it patches the real curve, not a guess
	// at an offset.
	std::vector<std::uint8_t> bytes(encoded->size());
	std::memcpy(bytes.data(), encoded->data(), encoded->size());
	const std::vector<std::uint16_t> table = lain::io::image::tiff::transferTable(ColorSpace::sRGB, 8);
	std::vector<std::uint8_t> needle(table.size() * sizeof(std::uint16_t));
	std::memcpy(needle.data(), table.data(), needle.size());
	const auto at = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
	REQUIRE(at != bytes.end()); // the curve really is in the file

	bytes[static_cast<std::size_t>(at - bytes.begin()) + 40] ^= 0x01;
	lain::memory::Buffer patched(bytes.size());
	std::memcpy(patched.data(), bytes.data(), bytes.size());

	const auto decoded = lain::io::image::decode("tiff", patched);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->colorSpace() == ColorSpace::Unspecified);
}

TEST_CASE("an unspecified alpha mode is written as unspecified, not invented as straight", "[io-image-tiff]")
{
	// The writer used to record EXTRASAMPLE_UNASSALPHA whatever it was told, so an image lain knew
	// nothing about came back Straight — a claim manufactured by the round trip itself.
	lain::io::image::tiff::registerCodec();

	for (const AlphaMode mode : {AlphaMode::Unspecified, AlphaMode::Straight, AlphaMode::Premultiplied})
	{
		const lain::image::Image original = rampImage(2, 2, PixelFormat::RGBA8, mode);
		const auto encoded = lain::io::image::encode("tiff", original);
		REQUIRE(encoded.has_value());
		const auto decoded = lain::io::image::decode("tiff", *encoded);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->alphaMode() == mode);
	}
}
