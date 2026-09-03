#include "jpegcolor.h"

#include <cstring>

namespace lain::io::image::jpeg
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// What the marker scan found, before it is collapsed to a ColorSpace. Kept as three
	// independent facts rather than an early return so the "which arm answered" question
	// (colorSpaceIsJfifConvention) is the same walk, not a second one that could disagree.
	struct MarkerFindings
	{
		bool icc = false;		// APP2 ICC_PROFILE
		bool jfif = false;		// APP0 JFIF
		bool exifSrgb = false;	// Exif ColorSpace == 1
		bool exifUncal = false; // Exif ColorSpace == 0xFFFF
	};

	static std::uint16_t be16(const std::uint8_t* p)
	{
		return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
	}

	static std::uint16_t read16(const std::uint8_t* p, bool bigEndian)
	{
		return bigEndian ? be16(p) : static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[1]) << 8) | p[0]);
	}

	static std::uint32_t read32(const std::uint8_t* p, bool bigEndian)
	{
		if (bigEndian)
		{
			return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
				   (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
		}
		return (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16) |
			   (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
	}

	// Walk one Exif IFD looking for `wanted`, following the ExifIFD pointer (0x8769) once.
	// `tiff` points at the TIFF header inside the APP1 payload; every offset in Exif is
	// relative to it, which is why it is passed rather than the segment start.
	//
	// Bounds are checked against the segment on every read: this parses attacker-supplied
	// bytes, and a truncated Exif block is common enough in real files to be ordinary rather
	// than exceptional.
	static bool findExifTag(const std::uint8_t* tiff, std::size_t tiffSize, std::uint32_t ifdOffset,
							std::uint16_t wanted, bool bigEndian, std::uint16_t& value, int depth)
	{
		if (depth > 2 || ifdOffset > tiffSize || tiffSize - ifdOffset < 2)
			return false;

		const std::uint16_t entries = read16(tiff + ifdOffset, bigEndian);
		if (tiffSize - ifdOffset < static_cast<std::size_t>(2) + static_cast<std::size_t>(entries) * 12)
			return false;

		std::uint32_t subIfd = 0;
		for (std::uint16_t i = 0; i < entries; ++i)
		{
			const std::uint8_t* entry = tiff + ifdOffset + 2 + static_cast<std::size_t>(i) * 12;
			const std::uint16_t tag = read16(entry, bigEndian);
			if (tag == wanted)
			{
				// A SHORT's value sits in the first 2 bytes of the 4-byte value field.
				value = read16(entry + 8, bigEndian);
				return true;
			}
			if (tag == 0x8769) // ExifIFD pointer — where ColorSpace actually lives
				subIfd = read32(entry + 8, bigEndian);
		}

		if (subIfd != 0)
			return findExifTag(tiff, tiffSize, subIfd, wanted, bigEndian, value, depth + 1);
		return false;
	}

	// The APP1 Exif segment: "Exif\0\0", then a TIFF header, then IFD0.
	static void scanExif(const std::uint8_t* payload, std::size_t size, MarkerFindings& found)
	{
		constexpr std::size_t kExifPrefix = 6; // "Exif\0\0"
		if (size < kExifPrefix + 8 || std::memcmp(payload, "Exif\0\0", kExifPrefix) != 0)
			return;

		const std::uint8_t* tiff = payload + kExifPrefix;
		const std::size_t tiffSize = size - kExifPrefix;

		bool bigEndian = true;
		if (std::memcmp(tiff, "II", 2) == 0)
			bigEndian = false;
		else if (std::memcmp(tiff, "MM", 2) != 0)
			return; // neither byte order marker: not a TIFF header

		if (read16(tiff + 2, bigEndian) != 42) // the TIFF magic
			return;

		std::uint16_t colorSpace = 0;
		if (findExifTag(tiff, tiffSize, read32(tiff + 4, bigEndian), 0xA001, bigEndian, colorSpace, 0))
		{
			if (colorSpace == 1)
				found.exifSrgb = true;
			else
				found.exifUncal = true; // 0xFFFF (Uncalibrated), or any other stated value
		}
	}

	// Walk the JPEG's marker segments. Stops at SOS / SOF / EOI: everything past those is
	// entropy-coded data, which this scan must never wander into.
	static MarkerFindings scanMarkers(const std::uint8_t* data, std::size_t size)
	{
		MarkerFindings found;
		if (data == nullptr || size < 4 || data[0] != 0xFF || data[1] != 0xD8) // SOI
			return found;

		std::size_t pos = 2;
		while (pos + 4 <= size)
		{
			if (data[pos] != 0xFF)
				break; // not a marker boundary — malformed; report what was found so far
			const std::uint8_t marker = data[pos + 1];
			if (marker == 0xFF)
			{
				++pos; // fill byte
				continue;
			}
			if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD9))
			{
				pos += 2; // standalone marker, no length
				continue;
			}
			if (marker == 0xDA) // SOS: entropy-coded data follows
				break;

			const std::size_t length = be16(data + pos + 2);
			if (length < 2 || pos + 2 + length > size)
				break; // truncated segment
			const std::uint8_t* payload = data + pos + 4;
			const std::size_t payloadSize = length - 2;

			if (marker == 0xE0 && payloadSize >= 5 && std::memcmp(payload, "JFIF\0", 5) == 0)
				found.jfif = true;
			else if (marker == 0xE1)
				scanExif(payload, payloadSize, found);
			else if (marker == 0xE2 && payloadSize >= 12 && std::memcmp(payload, "ICC_PROFILE\0", 12) == 0)
				found.icc = true;

			pos += 2 + length;
		}
		return found;
	}

	// The precedence, in one place so the two public functions cannot disagree about it.
	//
	// EXIF OUTRANKS THE PROFILE, which is the Exif specification's own arrangement rather than a
	// preference: ColorSpace is the field defined to answer this question, 1 means sRGB, and
	// Uncalibrated is what a file says when the answer lives in the profile instead. Ordering the
	// profile first would discard an explicit sRGB statement in the extremely common case of a
	// camera JPEG carrying both — reading LESS than the file states, which is its own kind of
	// wrong answer.
	static lain::image::ColorSpace decide(const MarkerFindings& found)
	{
		if (found.exifSrgb)
			return lain::image::ColorSpace::sRGB; // stated outright, in the field meant for it
		if (found.exifUncal)
			return lain::image::ColorSpace::Unspecified; // stated NOT to be sRGB
		if (found.icc)
			return lain::image::ColorSpace::Unspecified; // a profile lain can't represent
		if (found.jfif)
			return lain::image::ColorSpace::sRGB; // the conventional arm — see the header
		return lain::image::ColorSpace::Unspecified;
	}

	lain::image::ColorSpace colorSpaceFromMarkers(const std::uint8_t* data, std::size_t size)
	{
		return decide(scanMarkers(data, size));
	}

	bool colorSpaceIsJfifConvention(const std::uint8_t* data, std::size_t size)
	{
		const MarkerFindings found = scanMarkers(data, size);
		return found.jfif && !found.icc && !found.exifSrgb && !found.exifUncal;
	}

	bool jpegCanStateSpace(lain::image::ColorSpace space)
	{
		// Exhaustive with no default arm, so a new ColorSpace fails the build here rather than
		// silently inheriting whichever answer happened to sit last (-Werror,-Wswitch).
		switch (space)
		{
			case lain::image::ColorSpace::sRGB:		   // what the JFIF header this writer emits implies
			case lain::image::ColorSpace::Unspecified: // no claim; the reader's JFIF arm restates sRGB
				return true;

			case lain::image::ColorSpace::Linear:
			case lain::image::ColorSpace::BT709:
				return false; // no marker to write them into, and sRGB is what would be read back
		}
		return false;
	}
} // namespace lain::io::image::jpeg
