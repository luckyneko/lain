#pragma once

// How a container's colour tags become a lain::image::ColorSpace, and back — the whole of
// ADR-0018's colour rule for video, as pure functions so they can be tested across their arms
// without a file.
//
// BOTH DIRECTIONS LIVE IN ONE FILE ON PURPOSE. What the writer stamps, the reader must read back as
// the same ColorSpace, or lain's own round trip relabels footage — the silent wrong answer ADR-0018
// refuses on the way in, arriving on the way out. Splitting them across two files is how the two
// halves drift; a test asserts the composition is the identity over every space lain writes.

#include <lain/image/colorspace.h>

#include <optional>
#include <string>

extern "C"
{
#include <libavutil/pixfmt.h>
}

namespace lain::io::video::ffmpeg
{
	// The ColorSpace footage with these tags decodes to, or nullopt when lain refuses it.
	//
	// The rule, and its reasons (ADR-0018):
	//   - BT709, or ANY axis unspecified -> BT709. Encoded footage very often carries no colour
	//     tags at all, and FFmpeg's own convention of guessing BT.601 by frame size would make lain
	//     reject perfectly ordinary material for a tag it never carried. Guessing BT709 is right for
	//     everything modern; guessing BT.601 is loud and wrong.
	//   - The sRGB transfer (IEC 61966-2-1) -> sRGB. lain HAS that space and convert() composes
	//     through Linear since slice 4, so believing an explicit tag costs nothing and is the same
	//     rule the refusals follow. Screen recordings really do carry it.
	//   - Explicitly tagged BT.601, BT.2020, PQ or HLG -> REFUSED. They need different primaries or
	//     a real HDR story, and relabelling them would be a wrong answer in a calibration report —
	//     silently, since BT.601 and BT.709 are byte-identical and visually close.
	//
	// All three axes are examined, not just the transfer: BT.2020 material is often tagged only by
	// its matrix (which is exactly what the test fixture turned out to be), and ADR-0018's refusal
	// is about primaries as much as curves.
	[[nodiscard]] std::optional<lain::image::ColorSpace> colorSpaceFor(AVColorTransferCharacteristic transfer,
																	   AVColorPrimaries primaries,
																	   AVColorSpace matrix);

	// The matrix to decode a YUV frame against — the coefficient set, which is a SEPARATE question
	// from the ColorSpace above and has a different answer when the file says nothing.
	//
	// A tagged matrix is used as stated. An UNSPECIFIED one decodes as BT.601, where an unspecified
	// TRANSFER is treated as BT709: the two OETFs are the same curve to within rounding so the
	// transfer guess is free, while the matrices are far apart (17% on a saturated green) and an
	// untagged file is overwhelmingly one an encoder wrote with BT.601. See the reasoning, and the
	// measurement, in colorpolicy.cpp.
	//
	// The result is numerically an SWS_CS_* constant and can be handed straight to
	// sws_getCoefficients; FFmpeg aligns the two enumerations deliberately.
	[[nodiscard]] AVColorSpace decodeMatrixFor(AVColorSpace matrix);

	// Which tag caused a refusal, for the log line — "transfer SMPTE ST 2084 (PQ)". Empty when the
	// tags are acceptable. A refusal that did not name the tag would leave a user guessing which of
	// three axes to look at.
	[[nodiscard]] std::string refusedTagName(AVColorTransferCharacteristic transfer, AVColorPrimaries primaries,
											 AVColorSpace matrix);

	// The container tags to stamp on a stream carrying `space`.
	//
	// The exact inverse of colorSpaceFor. BT709 tags all three axes BT709; sRGB writes the
	// IEC 61966-2-1 transfer over BT709 primaries, because that tag and nothing else is what
	// colorSpaceFor believes. Linear and Unspecified never arrive — io::video::canEncode refuses
	// them at the seam, one level up, where the refusal is about the MEDIUM rather than about this
	// backend (Linear in particular would encode fine as AVCOL_TRC_LINEAR and read back as BT709
	// with every value off by the gamma, which is why it is refused rather than tolerated).
	//
	// `rgbPixelFormat` decides the matrix, and it is not a detail: an RGB-coded stream has had no
	// matrix applied, so tagging it BT709 would state a conversion that never happened.
	// AVCOL_SPC_RGB is what actually occurred, and colorSpaceFor accepts it — it is not one of the
	// refused matrices — so the round trip still closes.
	struct ColorTags
	{
		AVColorTransferCharacteristic transfer;
		AVColorPrimaries primaries;
		AVColorSpace matrix;
	};

	[[nodiscard]] std::optional<ColorTags> colorTagsFor(lain::image::ColorSpace space, bool rgbPixelFormat);
} // namespace lain::io::video::ffmpeg
