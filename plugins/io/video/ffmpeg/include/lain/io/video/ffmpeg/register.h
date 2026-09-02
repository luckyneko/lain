#pragma once

namespace lain::io::video::ffmpeg
{
	// Register this plugin's FFmpeg-backed reader into the io::video reader registry (under
	// "ffmpeg" — a BACKEND name, since FFmpeg is neither a container nor a codec but one
	// implementation covering both; see io/video/open.h). Called once by the generated
	// registerVideoCodecs() aggregator — or directly by a consumer that wants only this backend.
	// Nothing in the io::video core names FFmpeg.
	void registerCodec();
} // namespace lain::io::video::ffmpeg
