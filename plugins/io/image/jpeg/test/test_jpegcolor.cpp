// The JPEG marker scan as a pure function, over hand-built marker sequences — the same shape as
// the video plugin's test_colorpolicy.cpp, and for the same reason: every arm of the policy can be
// reached without owning a JPEG that happens to carry that combination of markers.
//
// These byte strings are deliberately NOT decodable images. The scan stops at SOS and never looks
// at entropy-coded data, so a header is all it needs — and a test that had to embed a real encoder's
// output for each case could not cover the contradictory combinations at all.

#include "jpegcolor.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using lain::image::ColorSpace;
using lain::io::image::jpeg::colorSpaceFromMarkers;
using lain::io::image::jpeg::colorSpaceIsJfifConvention;
using lain::io::image::jpeg::jpegCanStateSpace;

static void appendSegment(std::vector<std::uint8_t>& out, std::uint8_t marker, const std::vector<std::uint8_t>& payload)
{
	const std::size_t length = payload.size() + 2;
	out.push_back(0xFF);
	out.push_back(marker);
	out.push_back(static_cast<std::uint8_t>(length >> 8));
	out.push_back(static_cast<std::uint8_t>(length & 0xFF));
	out.insert(out.end(), payload.begin(), payload.end());
}

static std::vector<std::uint8_t> jfifPayload()
{
	return {'J', 'F', 'I', 'F', 0x00, 0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
}

static std::vector<std::uint8_t> iccPayload()
{
	return {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0x00, 0x01, 0x01};
}

// "Exif\0\0" + a big-endian TIFF header whose IFD0 holds only the ExifIFD pointer, and whose
// ExifIFD holds only ColorSpace (0xA001). This is the exact layout the real fixtures use.
static std::vector<std::uint8_t> exifPayload(std::uint16_t colorSpaceValue)
{
	std::vector<std::uint8_t> p{'E', 'x', 'i', 'f', 0x00, 0x00};
	const auto be16 = [&p](std::uint16_t v)
	{
		p.push_back(static_cast<std::uint8_t>(v >> 8));
		p.push_back(static_cast<std::uint8_t>(v & 0xFF));
	};
	const auto be32 = [&p](std::uint32_t v)
	{
		p.push_back(static_cast<std::uint8_t>(v >> 24));
		p.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
		p.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
		p.push_back(static_cast<std::uint8_t>(v & 0xFF));
	};

	p.push_back('M'); // big-endian
	p.push_back('M');
	be16(42); // the TIFF magic
	be32(8);  // IFD0 sits right after the header
	be16(1);  // IFD0: one entry
	be16(0x8769);
	be16(4); // LONG
	be32(1);
	be32(26); // -> the ExifIFD
	be32(0);  // no next IFD
	be16(1);  // ExifIFD: one entry
	be16(0xA001);
	be16(3); // SHORT
	be32(1);
	be16(colorSpaceValue); // a SHORT's value sits in the first half of the value field
	be16(0);
	be32(0); // no next IFD
	return p;
}

// SOI, the given segments, then SOS — where the scan stops.
static std::vector<std::uint8_t> jpegWith(const std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>>& segments)
{
	std::vector<std::uint8_t> out{0xFF, 0xD8};
	for (const auto& segment : segments)
		appendSegment(out, segment.first, segment.second);
	appendSegment(out, 0xDA, {0x01, 0x01, 0x00}); // SOS
	return out;
}

static ColorSpace spaceOf(const std::vector<std::uint8_t>& bytes)
{
	return colorSpaceFromMarkers(bytes.data(), bytes.size());
}

TEST_CASE("an explicit Exif ColorSpace of 1 reads as sRGB", "[io-image-jpeg][color]")
{
	const auto bytes = jpegWith({{0xE0, jfifPayload()}, {0xE1, exifPayload(1)}});
	REQUIRE(spaceOf(bytes) == ColorSpace::sRGB);
	REQUIRE_FALSE(colorSpaceIsJfifConvention(bytes.data(), bytes.size())); // the file said it
}

TEST_CASE("an Exif ColorSpace of Uncalibrated reads as Unspecified", "[io-image-jpeg][color]")
{
	// The file states it is NOT sRGB. That is a statement, and lain has no name for what it
	// points at — so the honest answer is Unspecified, not the JFIF convention underneath.
	const auto bytes = jpegWith({{0xE0, jfifPayload()}, {0xE1, exifPayload(0xFFFF)}});
	REQUIRE(spaceOf(bytes) == ColorSpace::Unspecified);
}

TEST_CASE("an ICC profile reads as Unspecified even beside a JFIF header", "[io-image-jpeg][color]")
{
	// This is the combination the old reader got wrong on an ordinary file: JFIF present, so it
	// answered sRGB, while the profile said the image was something else entirely.
	const auto bytes = jpegWith({{0xE0, jfifPayload()}, {0xE2, iccPayload()}});
	REQUIRE(spaceOf(bytes) == ColorSpace::Unspecified);
	REQUIRE_FALSE(colorSpaceIsJfifConvention(bytes.data(), bytes.size()));
}

TEST_CASE("an explicit Exif sRGB tag outranks a profile lain cannot hold", "[io-image-jpeg][color]")
{
	// A camera JPEG very often carries both. Ordering the profile first would throw away the one
	// statement lain can actually represent — reading LESS than the file says (see jpegcolor.cpp).
	const auto bytes = jpegWith({{0xE0, jfifPayload()}, {0xE1, exifPayload(1)}, {0xE2, iccPayload()}});
	REQUIRE(spaceOf(bytes) == ColorSpace::sRGB);
}

TEST_CASE("a JFIF header alone reads as sRGB, and reports itself as the convention", "[io-image-jpeg][color]")
{
	// The one conventional arm. It is flagged so the reader can say so in the log rather than
	// applying it silently, which is the distinction ADR-0020 draws.
	const auto bytes = jpegWith({{0xE0, jfifPayload()}});
	REQUIRE(spaceOf(bytes) == ColorSpace::sRGB);
	REQUIRE(colorSpaceIsJfifConvention(bytes.data(), bytes.size()));
}

TEST_CASE("a JPEG with no APP marker at all reads as Unspecified", "[io-image-jpeg][color]")
{
	const auto bytes = jpegWith({});
	REQUIRE(spaceOf(bytes) == ColorSpace::Unspecified);
	REQUIRE_FALSE(colorSpaceIsJfifConvention(bytes.data(), bytes.size()));
}

TEST_CASE("malformed and truncated marker data yields Unspecified, not a partial reading", "[io-image-jpeg][color]")
{
	// The scan parses bytes it did not produce, so every one of these must terminate rather than
	// walk off the buffer. Catch2 reports a crash as a failure, which is the assertion that matters.
	REQUIRE(colorSpaceFromMarkers(nullptr, 0) == ColorSpace::Unspecified);

	const std::vector<std::uint8_t> notJpeg{'n', 'o', 't', ' ', 'a', ' ', 'j', 'p', 'g'};
	REQUIRE(spaceOf(notJpeg) == ColorSpace::Unspecified);

	// A segment claiming more bytes than the buffer holds.
	std::vector<std::uint8_t> truncated{0xFF, 0xD8, 0xFF, 0xE1, 0x7F, 0xFF, 'E', 'x'};
	REQUIRE(spaceOf(truncated) == ColorSpace::Unspecified);

	// A well-formed Exif segment cut off mid-IFD.
	auto shortExif = exifPayload(1);
	shortExif.resize(shortExif.size() - 12);
	REQUIRE(spaceOf(jpegWith({{0xE1, shortExif}})) == ColorSpace::Unspecified);
}

TEST_CASE("the writer can state only the space JFIF implies", "[io-image-jpeg][color]")
{
	// stb emits a fixed JFIF header and no marker this codec could write a space into, so Linear
	// and BT709 are refused rather than written and read back as sRGB.
	REQUIRE(jpegCanStateSpace(ColorSpace::sRGB));
	REQUIRE(jpegCanStateSpace(ColorSpace::Unspecified));
	REQUIRE_FALSE(jpegCanStateSpace(ColorSpace::Linear));
	REQUIRE_FALSE(jpegCanStateSpace(ColorSpace::BT709));
}
