#pragma once

// How a PNG's colour chunks become a lain::image::ColorSpace, and back — this codec's whole
// share of ADR-0020, as functions over libpng's read/write structs.
//
// BOTH DIRECTIONS LIVE IN ONE FILE ON PURPOSE, for the reason the video codec's colorpolicy.h
// gives: what the writer stamps, the reader must read back as the same ColorSpace, or lain's own
// round trip relabels an image. Splitting them across two files is how the two halves drift —
// which is exactly what happened here before ADR-0020, the writer having recorded no colour chunk
// at all while the reader read three.

#include <lain/image/colorspace.h>

#include <png.h>

namespace lain::io::image::png
{
	// The ColorSpace this PNG's chunks declare — honestly, without guessing.
	//
	// An sRGB chunk is definitive. An iCCP profile is Unspecified: lain cannot represent an
	// arbitrary profile, and "a space we can't hold" is not "no space" — a caller must decide.
	// A gAMA near 1/2.2 or 1.0 maps to sRGB / Linear; anything else, or no colour chunk at all,
	// is Unspecified.
	//
	// The gAMA window is the ONE approximation in this file, and ADR-0020 names it as such: gAMA
	// states an exponent, not a standard, and sRGB's curve is not a pure 1/2.2 power law. It is
	// kept because a gAMA-only PNG is common and the alternative is discarding the only colour
	// statement such a file makes. Note lain WRITES gAMA 1.0 for Linear and relies on this window
	// to read it back, so the Linear arm is exact for lain's own files.
	[[nodiscard]] lain::image::ColorSpace colorSpaceFromPng(png_structp png, png_infop info);

	// Whether PNG can state `space` at all — the writer's half of ADR-0020's "record it, or refuse".
	//
	// BT709 is the refusal, and it is not a gap in libpng: PNG's only transfer handle is gAMA,
	// BT709's exponent is 0.45, and colorSpaceFromPng's window reads 0.45 back as sRGB. Writing it
	// would produce a file lain itself relabels — the silent mislabel ADR-0020 exists to remove.
	// A caller converts to sRGB, or writes TIFF, which states the curve exactly.
	[[nodiscard]] bool pngCanStateSpace(lain::image::ColorSpace space);

	// Whether PNG can hold `mode` for a format that carries alpha.
	//
	// PNG's alpha is unassociated by specification, so Premultiplied is REFUSED rather than
	// written: the bytes would survive and colorSpaceFromPng's caller would tag them Straight,
	// which is data corruption of exactly the kind ImageWriter::canEncode exists to prevent
	// (see io/image/writer.h). Unspecified is ACCEPTED, and comes back Straight — that is the
	// format supplying a fact it guarantees, not lain inventing one.
	[[nodiscard]] bool pngCanStateAlpha(lain::image::AlphaMode mode);

	// Record `space` into the info struct. Must be called before png_write_info.
	//
	// The exact inverse of colorSpaceFromPng. sRGB writes the sRGB chunk *plus* the gAMA and cHRM
	// libpng derives from it, so a reader that understands only gAMA also lands on the right curve;
	// lain's own reader takes the sRGB chunk first either way. Unspecified writes nothing, which is
	// how a PNG says "no claim" and is what makes that value round-trip exactly. BT709 never
	// arrives — pngCanStateSpace refuses it at canEncode.
	void applyColorSpaceToPng(png_structp png, png_infop info, lain::image::ColorSpace space);
} // namespace lain::io::image::png
