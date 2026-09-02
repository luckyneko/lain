#pragma once

// How a container's colour tags become a lain::image::ColorSpace — the whole of ADR-0018's colour
// rule for video, as one pure function so it can be tested across its arms without a file.

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

	// Which tag caused a refusal, for the log line — "transfer SMPTE ST 2084 (PQ)". Empty when the
	// tags are acceptable. A refusal that did not name the tag would leave a user guessing which of
	// three axes to look at.
	[[nodiscard]] std::string refusedTagName(AVColorTransferCharacteristic transfer, AVColorPrimaries primaries,
											 AVColorSpace matrix);
} // namespace lain::io::video::ffmpeg
