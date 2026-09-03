// The encoder candidate lists — pure data, so these need no file and no encode.
//
// The most valuable case here is the licence one. ADR-0019's central claim is that lain never links
// or calls a GPL encoder, and a claim that is only prose is one nobody notices breaking.

#include "encoderpolicy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
}

using lain::io::video::VideoCodec;
using lain::io::video::ffmpeg::encoderCandidates;

namespace
{
	constexpr VideoCodec allFamilies[] = {VideoCodec::Auto, VideoCodec::H264, VideoCodec::HEVC,
										  VideoCodec::ProRes, VideoCodec::FFV1, VideoCodec::MJPEG};
}

TEST_CASE("every codec family has candidates, and auto is the delivery list", "[video][ffmpeg]")
{
	for (const VideoCodec family : allFamilies)
		CHECK_FALSE(encoderCandidates(family).empty());

	// Auto means DELIVERY-or-refuse. If it ever quietly became an archival list, a delivery render
	// would silently change size by an order of magnitude and audience entirely.
	CHECK(encoderCandidates(VideoCodec::Auto) == encoderCandidates(VideoCodec::H264));
}

TEST_CASE("no GPL encoder is named, or linked", "[video][ffmpeg]")
{
	// Half the claim: lain never asks for one.
	for (const VideoCodec family : allFamilies)
	{
		for (const std::string& candidate : encoderCandidates(family))
		{
			CHECK(candidate.find("libx264") == std::string::npos);
			CHECK(candidate.find("libx265") == std::string::npos);
		}
	}

	// The other half, and the one that matters: the LINKED library does not have them. Linking a
	// build configured with libx264/libx265 relicenses the whole distribution regardless of which
	// encoder is actually called (ADR-0019), so their absence is the licence property itself —
	// not a coding convention. This sits beside test_build.cpp's configuration probe for the same
	// reason: a licence claim has to be executable.
	CHECK(avcodec_find_encoder_by_name("libx264") == nullptr);
	CHECK(avcodec_find_encoder_by_name("libx265") == nullptr);
}

TEST_CASE("hardware wrappers are candidates but frame-context-only ones are not", "[video][ffmpeg]")
{
	const std::vector<std::string> h264 = encoderCandidates(VideoCodec::H264);
	const auto has = [&h264](const std::string& name)
	{
		return std::find(h264.begin(), h264.end(), name) != h264.end();
	};

	// These take SOFTWARE frames, so this writer can feed them.
	CHECK(has("h264_videotoolbox"));
	CHECK(has("h264_nvenc"));

	// These do not: they need an AVHWFramesContext and an uploaded surface, which this writer has
	// no path to build. Listing them would turn a clean "no delivery encoder here" into a
	// confusing failure to open (ADR-0019, amended).
	CHECK_FALSE(has("h264_vaapi"));
	CHECK_FALSE(has("h264_v4l2m2m"));
}
