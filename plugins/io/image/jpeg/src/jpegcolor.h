#pragma once

// What a JPEG's APP markers say about its colour space, and what this codec can say back —
// this codec's share of ADR-0020, as pure functions over the encoded bytes.
//
// BOTH DIRECTIONS LIVE IN ONE FILE, for the reason the video codec's colorpolicy.h gives. Here
// they are deliberately ASYMMETRIC and that asymmetry is the point: the reader can distinguish
// four cases, the writer can state exactly one, so what it cannot state it refuses.
//
// This scanner exists because stb_image discards every APP marker — it decodes pixels and nothing
// else. Before ADR-0020 that left the reader asserting sRGB for every JPEG it ever saw, including
// one carrying an Adobe RGB profile or an explicit "not sRGB" Exif tag.

#include <lain/image/colorspace.h>

#include <cstddef>
#include <cstdint>

namespace lain::io::image::jpeg
{
	// The ColorSpace this JPEG's APP markers declare.
	//
	// First match wins, most specific first:
	//   - APP1 Exif, ColorSpace tag (0xA001) == 1 -> sRGB. The file says so, in the field the Exif
	//     specification defines to answer exactly this question.
	//   - APP1 Exif, ColorSpace tag == 0xFFFF (Uncalibrated) -> Unspecified. The file says it is
	//     NOT sRGB, which is a statement, and not one lain can name.
	//   - APP2 "ICC_PROFILE" -> Unspecified. An embedded profile is a colour space lain cannot
	//     represent, and "one we can't hold" is not "none" (the same arm as PNG's iCCP).
	//   - APP0 "JFIF" with nothing above contradicting it -> sRGB.
	//   - no APP0/APP1/APP2 at all -> Unspecified.
	//
	// THE JFIF ARM IS THE ONE CONVENTIONAL ANSWER IN THIS FILE, and ADR-0020 names it as such.
	// JFIF fixes BT.601 primaries and states no transfer at all, so "JFIF implies sRGB" is
	// universal practice rather than something the specification says. It is kept because a
	// JFIF-only JPEG is the overwhelmingly common case and treating it as Unspecified would make
	// the format unusable without a hand-placed declaration. What it is NOT is the old behaviour:
	// any of the arms above overrides it, so a profile-bearing or explicitly-uncalibrated JPEG is
	// no longer claimed as sRGB.
	//
	// Malformed or truncated marker data yields Unspecified rather than a partial reading — the
	// scan is bounds-checked throughout and never decodes entropy-coded data.
	[[nodiscard]] lain::image::ColorSpace colorSpaceFromMarkers(const std::uint8_t* data, std::size_t size);

	// Whether the JFIF arm above (rather than an explicit marker) is what produced sRGB — the
	// caller logs that, since a convention applied silently is the thing ADR-0020 objects to.
	[[nodiscard]] bool colorSpaceIsJfifConvention(const std::uint8_t* data, std::size_t size);

	// Whether this codec can record `space`.
	//
	// stb_image_write emits a fixed JFIF header and offers no way to inject a marker, so the only
	// space a JPEG written here can state is the one JFIF conventionally implies. Linear and BT709
	// are REFUSED rather than written: the bytes would survive and this codec's own reader would
	// report them as sRGB — a tag the writer never wrote, over pixels that are a whole transfer
	// curve away from it. That was the worst silent path in the image stack before ADR-0020.
	[[nodiscard]] bool jpegCanStateSpace(lain::image::ColorSpace space);
} // namespace lain::io::image::jpeg
