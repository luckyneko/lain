#pragma once

// Which encoder serves which codec family — the availability question of ADR-0019, as a pure list
// so it can be read and tested without opening a file.

#include <lain/io/video/writer.h>

#include <string>
#include <vector>

namespace lain::io::video::ffmpeg
{
	// The encoder names that could serve `codec`, best first — NAMES ONLY, resolved against nothing.
	//
	// ONE LIST FOR EVERY PLATFORM, AND NO #ifdef ANYWHERE. That is the point: availability is a
	// runtime fact about a build, and avcodec_find_encoder_by_name simply returns null for a wrapper
	// this FFmpeg was not configured with. Making the platform conditionality DATA rather than
	// preprocessor means the selector is one code path tested identically everywhere, and a build
	// that gains an encoder gains it with no source edit — which is what ADR-0019 means by
	// "selects by availability rather than by a hardcoded name".
	//
	// libx264 and libx265 appear in no list and never can. They are GPL, and linking a build
	// configured with them relicenses the whole distribution regardless of which encoder is actually
	// called; test_encoderpolicy.cpp asserts both that they are absent from these lists and that the
	// linked library does not have them.
	//
	// DELIVERY CANDIDATES ARE NARROWER THAN ADR-0019'S TABLE, and deliberately: **VAAPI and
	// V4L2-M2M are not here**. They cannot accept software frames — they require an
	// AVHWFramesContext and an uploaded surface, which this writer has no path to build — so
	// listing them would turn a clean "this build has no delivery encoder" into a confusing failure
	// to open. The videotoolbox, NVENC and Media Foundation wrappers all take software frames,
	// which is why they stay. The consequence is real and is stated in ADR-0019: a Linux machine
	// with no NVIDIA card has no delivery encoder here, and says so.
	[[nodiscard]] std::vector<std::string> encoderCandidates(VideoCodec codec);

	// Whether `codec` is a LOSSLESS family, which decides whether the writer prefers an RGB pixel
	// format over the yuv420p every player expects.
	//
	// It is not a preference — it is what makes the word lossless true. An 8-bit RGB->YUV matrix is
	// not invertible, so FFV1 over yuv420p is a lossless encoding of a lossy conversion: the file
	// round-trips, the FRAMES do not.
	[[nodiscard]] bool isLossless(VideoCodec codec);

	// The family's name for a log line ("h264", "ffv1") — lowercase, matching what a caller types.
	[[nodiscard]] std::string codecName(VideoCodec codec);
} // namespace lain::io::video::ffmpeg
