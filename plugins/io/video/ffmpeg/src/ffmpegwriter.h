#pragma once

// The FFmpeg-backed VideoWriter — with the reader, the only code in lain that names libavformat /
// libavcodec.
//
// It fills the whole io::video::VideoWriter contract: choose an encoder by availability, open a
// muxer over lain's transport, encode and mux frames in order, and finalise. That single interface
// spans both aspects of a video file (container and codec) for the reason the registry it registers
// into records (lain/io/video/open.h) — and this is the side where the distinction becomes visible
// as ADR-0019 predicted: the CONTAINER follows the file's name, the CODEC is an option resolved
// against what this build actually has.

#include <lain/io/stream.h>
#include <lain/io/video/save.h>
#include <lain/io/video/writer.h>

#include <cstdint>
#include <memory>
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace lain::io::video::ffmpeg
{
	class FFmpegVideoWriter : public lain::io::video::VideoWriter
	{
	public:
		FFmpegVideoWriter() = default;
		~FFmpegVideoWriter() override;

		bool open(std::unique_ptr<lain::io::WriteStream> stream, std::string_view format,
				  const lain::media::FrameSpec& spec, const VideoWriterOptions& options) override;

		const std::string& container() const override { return m_container; }
		const std::string& codec() const override { return m_codec; }

		bool write(const lain::image::Image& image) override;
		bool finish() override;

	private:
		// Try one named encoder into `oformat`. Returns true with m_encoder open, or false having
		// logged at INFO why this candidate could not serve — "not in this build", "this container
		// will not carry it", "it would not open". The last is the only test that is TRUE: an
		// encoder can exist in a build and fail to open for want of a device (NVENC with no NVIDIA
		// card), which is information about the machine rather than a fault.
		[[nodiscard]] bool tryEncoder(const std::string& name, const AVOutputFormat* oformat,
									  const VideoWriterOptions& options);

		// The pixel format to encode into: an RGB one for a LOSSLESS family (or the word is a lie —
		// an 8-bit RGB->YUV matrix is not invertible), else yuv420p where offered, else the
		// encoder's first.
		[[nodiscard]] static AVPixelFormat choosePixelFormat(const AVCodec& encoder, bool lossless);

		[[nodiscard]] bool encodeAndMux(AVFrame* frame);
		void closeAll();

		std::unique_ptr<lain::io::WriteStream> m_stream;
		AVIOContext* m_avio = nullptr;
		AVFormatContext* m_format = nullptr;
		AVCodecContext* m_encoder = nullptr;
		AVStream* m_videoStream = nullptr;
		SwsContext* m_scaler = nullptr;
		AVPacket* m_packet = nullptr;
		AVFrame* m_frame = nullptr;

		lain::media::FrameSpec m_spec;
		std::string m_container;
		std::string m_codec;
		AVPixelFormat m_pixelFormat = AV_PIX_FMT_NONE;

		// Frames accepted so far. It IS the presentation timestamp: the stream timebase is the
		// reciprocal of the spec's rate, so frame n has pts n, which is what makes the output
		// constant-rate and exactly as long as the number of frames written.
		std::int64_t m_written = 0;

		// av_write_trailer on a container whose header was never written is undefined, and finish()
		// must run after a failed open as well as a successful one.
		bool m_headerWritten = false;
		bool m_finished = false;
		bool m_finishStatus = false;
	};
} // namespace lain::io::video::ffmpeg
