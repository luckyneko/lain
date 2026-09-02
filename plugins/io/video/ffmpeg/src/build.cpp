#include "lain/io/video/ffmpeg/build.h"

extern "C"
{
#include <libavutil/avutil.h>
}

namespace lain::io::video::ffmpeg
{
	std::string configuration()
	{
		// Both return pointers into FFmpeg's own static storage, so they cannot be null for a
		// library that loaded at all — but a std::string from a null pointer is undefined
		// rather than empty, and this is the one place that would be asked about a library
		// too broken to trust. Cheap insurance at a seam whose whole job is to be believed.
		const char* text = avutil_configuration();
		return text != nullptr ? std::string{text} : std::string{};
	}

	std::string license()
	{
		const char* text = avutil_license();
		return text != nullptr ? std::string{text} : std::string{};
	}
} // namespace lain::io::video::ffmpeg
