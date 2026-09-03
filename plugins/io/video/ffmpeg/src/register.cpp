#include "lain/io/video/ffmpeg/register.h"

#include "ffmpegreader.h"
#include "ffmpegwriter.h"

#include <lain/io/video/open.h> // readerRegistry
#include <lain/io/video/save.h> // writerRegistry

namespace lain::io::video::ffmpeg
{
	void registerCodec()
	{
		// "ffmpeg" is a BACKEND name, not a container or a codec: this one implementation covers
		// both aspects of a video file, and a platform-native alternative would replace both at
		// once (lain/io/video/open.h carries the reasoning). It is the whole attachment surface —
		// nothing in the codec-free seam names FFmpeg.
		lain::io::video::readerRegistry().registerType<FFmpegVideoReader>("ffmpeg");

		// Its reader and its writer arrive together, so a build that can open an mp4 can also write
		// one — the rule plugins/io/image/png already follows, and the reason a caller never has to
		// ask which half of a codec plugin it got.
		lain::io::video::writerRegistry().registerType<FFmpegVideoWriter>("ffmpeg");
	}
} // namespace lain::io::video::ffmpeg
