#pragma once

// FFmpeg's AVIO callbacks over lain's Stream — the whole transport bridge, in one place for both
// directions.
//
// FFmpeg moves bytes through these callbacks and never sees a filename: the transport is lain's
// (ADR-0004), which is what keeps every fstream out of a codec plugin and what will let a future
// s3:// scheme reach the decoder and the muxer unchanged. The prebuilt is --disable-network, so
// libavformat could not open a remote url even if it were handed one.
//
// THE READER AND THE WRITER NEED THE SAME SEEK CALLBACK, because seeking is io::Stream's, not
// ReadStream's or WriteStream's: AVSEEK_SIZE is a question rather than a move (it is what lets
// libavformat probe an MP4's tail without reading everything before it, and what lets a muxer patch
// a size it wrote earlier), AVSEEK_FORCE is advisory and masked off, and the three whences map onto
// SeekOrigin identically. Two copies of that would drift on the first fix, which is why this file
// exists rather than the writer growing its own half.
//
// The opaque pointer is ALWAYS an io::Stream*, never a subclass — casting a void* back to a type it
// was not stored as is not something to be clever about, and passing the base is what lets one seek
// callback serve both directions honestly.

#include <lain/io/stream.h>

extern "C"
{
#include <libavformat/avio.h>
}

namespace lain::io::video::ffmpeg
{
	// FFmpeg is free to replace this buffer, which is why freeContext frees the one the context
	// holds rather than the one that was handed in.
	inline constexpr int avioBufferSize = 32768;

	// An AVIOContext reading from / writing to `stream`, or nullptr with the reason logged.
	// The stream must outlive the context; both callers own theirs as a member.
	[[nodiscard]] AVIOContext* makeReadContext(lain::io::ReadStream& stream);
	[[nodiscard]] AVIOContext* makeWriteContext(lain::io::WriteStream& stream);

	// Free `avio` and whatever buffer it is holding NOW, then null the pointer. A no-op on null.
	void freeContext(AVIOContext*& avio);
} // namespace lain::io::video::ffmpeg
