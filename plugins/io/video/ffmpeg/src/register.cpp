#include "lain/io/video/ffmpeg/register.h"

#include "ffmpegreader.h"

#include <lain/io/video/open.h> // readerRegistry

namespace lain::io::video::ffmpeg
{
	void registerCodec()
	{
		// "ffmpeg" is a BACKEND name, not a container or a codec: this one implementation covers
		// both aspects of a video file, and a platform-native alternative would replace both at
		// once (lain/io/video/open.h carries the reasoning). It is the whole attachment surface —
		// nothing in the codec-free seam names FFmpeg.
		lain::io::video::readerRegistry().registerType<FFmpegVideoReader>("ffmpeg");
	}
} // namespace lain::io::video::ffmpeg
