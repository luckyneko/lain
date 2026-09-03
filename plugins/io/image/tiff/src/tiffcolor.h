#pragma once

// How a TIFF's colour tags become a lain::image::ColorSpace, and back — this codec's share of
// ADR-0020, over libtiff's directory.
//
// BOTH DIRECTIONS LIVE IN ONE FILE ON PURPOSE, for the reason the video codec's colorpolicy.h
// gives: what the writer stamps, the reader must read back as the same ColorSpace, or lain's own
// round trip relabels an image.
//
// TIFF has no sRGB flag — which is why this reader answered Unspecified unconditionally before
// ADR-0020 — but it does have TransferFunction, a sampled curve that states a transfer EXACTLY
// and needs no embedded profile. That makes TIFF the one still format here that can carry BT709
// (PNG's only handle is gAMA, whose 0.45 the PNG reader cannot tell from sRGB).

#include <lain/image/colorspace.h>

#include <tiffio.h>

#include <cstdint>
#include <vector>

namespace lain::io::image::tiff
{
	// The TransferFunction table lain writes for `space` at `bits`: entry i maps the stored code i
	// to the linear intensity it stands for, scaled across the full uint16 range, which is what the
	// TIFF specification defines the table to hold. It samples image::toLinear, so the curve
	// written, the curve recognised and the curve image::convert applies are one function.
	//
	// Public because it is also what a caller needs to ASK what lain would write — which is how the
	// tests find the tag inside an encoded file rather than guessing at an offset, and how they
	// avoid a second spelling of the formula that could drift from this one.
	[[nodiscard]] std::vector<std::uint16_t> transferTable(lain::image::ColorSpace space, std::uint16_t bits);

	// The ColorSpace this directory's tags declare.
	//
	//   - An ICC profile -> Unspecified. lain cannot represent an arbitrary profile, and one it
	//     can't hold is not "none" (the same arm as PNG's iCCP and JPEG's APP2).
	//   - A TransferFunction bit-identical to the table lain would write for a space -> that space.
	//   - Anything else, including no colour tag at all -> Unspecified.
	//
	// THE COMPARISON IS EXACT, not a tolerance window, and that is what keeps this a fact rather
	// than a second gAMA-style heuristic: lain's own files match by construction, and a foreign
	// curve is claimed only if it is bit-for-bit what lain would have written. A near-miss is
	// reported as Unspecified — the caller declares, which is the contract images already have.
	//
	// `bits` and `colorChannels` must be the values from this directory: TIFF fixes the table
	// length at 2^BitsPerSample and stores one array for a single colour channel, three otherwise.
	[[nodiscard]] lain::image::ColorSpace colorSpaceFromTiff(TIFF* tif, std::uint16_t bits, std::uint16_t colorChannels);

	// Whether TIFF can state `space`. Every one of them, which is what makes this the fallback
	// format when PNG or JPEG refuses.
	[[nodiscard]] bool tiffCanStateSpace(lain::image::ColorSpace space);

	// Record `space` as a TransferFunction on the open directory. Unspecified writes no tag —
	// the honest absence of a claim, and what makes that value round-trip exactly.
	//
	// MUST be called after BitsPerSample, SamplesPerPixel and ExtraSamples are set: libtiff reads
	// 2^BitsPerSample entries from each array it is handed, and decides how many arrays to expect
	// from the colour-channel count. Setting it earlier reads the wrong length off the caller's
	// buffer.
	//
	// Cost, stated rather than found later: the table is 2^BitsPerSample uint16 entries per colour
	// channel, so it is 1.5 KB on an 8-bit RGB image and 384 KB on a 16-bit one. That is set by the
	// TIFF specification, not by this choice. It is negligible beside the 16-bit images anyone
	// actually writes (under 1% of a 4K frame) and disproportionate only for a tiny one.
	void applyColorSpaceToTiff(TIFF* tif, lain::image::ColorSpace space, std::uint16_t bits,
							   std::uint16_t colorChannels);
} // namespace lain::io::image::tiff
