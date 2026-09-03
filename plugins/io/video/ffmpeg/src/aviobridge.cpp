#include "aviobridge.h"

#include <lain/log/log.h>

#include <cstddef>
#include <cstdint>
#include <optional>

extern "C"
{
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace lain::io::video::ffmpeg
{
	static int readPacket(void* opaque, std::uint8_t* buffer, int size)
	{
		auto* stream = static_cast<lain::io::ReadStream*>(opaque);
		const std::optional<std::size_t> read = stream->read(reinterpret_cast<std::byte*>(buffer),
															 static_cast<std::size_t>(size));
		if (!read)
			return AVERROR(EIO); // a failure, which is NOT the end of the file
		if (*read == 0)
			return AVERROR_EOF;
		return static_cast<int>(*read);
	}

	// Note the const on the buffer, which the read callback does not have: that asymmetry is
	// FFmpeg's own signature (libavformat/avio.h), not an oversight to be tidied up.
	static int writePacket(void* opaque, const std::uint8_t* buffer, int size)
	{
		auto* stream = static_cast<lain::io::WriteStream*>(opaque);
		if (!stream->write(reinterpret_cast<const std::byte*>(buffer), static_cast<std::size_t>(size)))
			return AVERROR(EIO);

		// The whole count or nothing — WriteStream::write reports a partial transfer as a failure,
		// so there is no short-write case to hand back.
		return size;
	}

	static std::int64_t seekPacket(void* opaque, std::int64_t offset, int whence)
	{
		auto* stream = static_cast<lain::io::Stream*>(opaque);

		// AVSEEK_SIZE is a question, not a move — answering it is what lets libavformat probe a
		// container's tail (where an MP4 keeps its index) without reading everything before it,
		// and what lets a muxer find out how much it has written so far.
		if ((whence & AVSEEK_SIZE) != 0)
		{
			const std::optional<std::uint64_t> size = stream->size();
			return size ? static_cast<std::int64_t>(*size) : AVERROR(ENOSYS);
		}

		lain::io::SeekOrigin from = lain::io::SeekOrigin::Begin;
		switch (whence & ~AVSEEK_FORCE)
		{
			case SEEK_SET:
				from = lain::io::SeekOrigin::Begin;
				break;
			case SEEK_CUR:
				from = lain::io::SeekOrigin::Current;
				break;
			case SEEK_END:
				from = lain::io::SeekOrigin::End;
				break;
			default:
				return AVERROR(EINVAL);
		}

		const std::optional<std::uint64_t> position = stream->seek(offset, from);
		return position ? static_cast<std::int64_t>(*position) : AVERROR(EINVAL);
	}

	static AVIOContext* makeContext(lain::io::Stream& stream, int writable,
									int (*readFn)(void*, std::uint8_t*, int),
									int (*writeFn)(void*, const std::uint8_t*, int))
	{
		auto* buffer = static_cast<unsigned char*>(av_malloc(avioBufferSize));
		if (buffer == nullptr)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating the IO buffer");
			return nullptr;
		}

		AVIOContext* avio = avio_alloc_context(buffer, avioBufferSize, writable, &stream, readFn, writeFn,
											   &seekPacket);
		if (avio == nullptr)
		{
			av_free(buffer);
			lain::log::error("io::video::ffmpeg: could not create the IO context for {}", stream.uri());
		}
		return avio;
	}

	AVIOContext* makeReadContext(lain::io::ReadStream& stream)
	{
		return makeContext(stream, 0, &readPacket, nullptr);
	}

	AVIOContext* makeWriteContext(lain::io::WriteStream& stream)
	{
		// write_flag = 1, and the callback goes in the sixth slot rather than the fifth. That one
		// argument is the whole structural difference between the two directions.
		return makeContext(stream, 1, nullptr, &writePacket);
	}

	void freeContext(AVIOContext*& avio)
	{
		if (avio == nullptr)
			return;

		// FFmpeg may have replaced the buffer we handed it, so the one to free is the one the
		// context holds now — freeing our original pointer would free the wrong allocation.
		av_freep(&avio->buffer);
		avio_context_free(&avio);
	}
} // namespace lain::io::video::ffmpeg
