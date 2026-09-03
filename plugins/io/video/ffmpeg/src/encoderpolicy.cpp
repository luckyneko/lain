#include "encoderpolicy.h"

namespace lain::io::video::ffmpeg
{
	std::vector<std::string> encoderCandidates(VideoCodec codec)
	{
		switch (codec)
		{
			case VideoCodec::Auto:
				// Auto is DELIVERY, and it refuses rather than sliding into an archival family: a
				// render that quietly became ProRes or MJPEG has changed its size by an order of
				// magnitude and its audience entirely, which is a wrong answer that looks like
				// success. Same argument the missing-frame policy makes about a shortened video.
				[[fallthrough]];
			case VideoCodec::H264:
				return {"h264_videotoolbox", "h264_nvenc", "h264_mf"};

			case VideoCodec::HEVC:
				return {"hevc_videotoolbox", "hevc_nvenc", "hevc_mf"};

			case VideoCodec::ProRes:
				// The hardware encoder first where it exists, then FFmpeg's own two. All three are
				// LGPL-safe: the videotoolbox one encodes in the operating system, and prores_ks /
				// prores are native.
				return {"prores_videotoolbox", "prores_ks", "prores"};

			case VideoCodec::FFV1:
				return {"ffv1"};

			case VideoCodec::MJPEG:
				return {"mjpeg"};
		}
		return {};
	}

	bool isLossless(VideoCodec codec)
	{
		return codec == VideoCodec::FFV1;
	}

	std::string codecName(VideoCodec codec)
	{
		switch (codec)
		{
			case VideoCodec::Auto:
				return "auto";
			case VideoCodec::H264:
				return "h264";
			case VideoCodec::HEVC:
				return "hevc";
			case VideoCodec::ProRes:
				return "prores";
			case VideoCodec::FFV1:
				return "ffv1";
			case VideoCodec::MJPEG:
				return "mjpeg";
		}
		return "unknown";
	}
} // namespace lain::io::video::ffmpeg
