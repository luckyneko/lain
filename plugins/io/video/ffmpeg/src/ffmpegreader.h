#pragma once

// The FFmpeg-backed VideoReader — the only class in lain that names libavformat / libavcodec.
//
// It fills the whole io::video::VideoReader contract: demux, frame table, seek, decode, convert.
// That single interface spans both aspects of a video file (container and codec) on purpose; the
// reasoning for not splitting it in two lives on the registry it is registered into
// (lain/io/video/open.h), because that is where someone would go looking to split it.

#include <lain/io/stream.h>
#include <lain/io/video/reader.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace lain::io::video::ffmpeg
{
	class FFmpegVideoReader : public lain::io::video::VideoReader
	{
	public:
		FFmpegVideoReader() = default;
		~FFmpegVideoReader() override;

		bool open(std::unique_ptr<lain::io::ReadStream> stream, lain::media::FrameRate fallbackRate) override;

		const lain::media::FrameSpec& spec() const override { return m_spec; }
		std::size_t frameCount() const override { return m_table.size(); }
		const std::string& container() const override { return m_container; }
		const std::string& codec() const override { return m_codec; }

		lain::core::Time timestamp(std::size_t ordinal) const override;
		lain::image::Image decode(std::size_t ordinal) override;

	private:
		// One frame as the CONTAINER describes it, before anything is decoded. This is the private
		// frame table CONTEXT.md describes; slice 1 deliberately shipped no public type for it,
		// because two of these three fields mean nothing for a folder of stills.
		struct Entry
		{
			std::int64_t pts = 0;  // presentation timestamp, in stream time_base units
			std::int64_t pos = 0;  // byte offset of the packet, -1 when the container will not say
			bool keyframe = false; // a point the decoder can be started from
		};

		[[nodiscard]] bool buildFrameTable();
		[[nodiscard]] bool startDecoder(const AVCodec* decoder, const AVCodecParameters* parameters);
		[[nodiscard]] bool seekTo(std::size_t ordinal);
		[[nodiscard]] lain::image::Image decodeNextMatching(std::int64_t targetPts);
		[[nodiscard]] lain::image::Image convert(const AVFrame& frame);
		void closeAll();

		std::unique_ptr<lain::io::ReadStream> m_stream;
		AVIOContext* m_avio = nullptr;
		AVFormatContext* m_format = nullptr;
		AVCodecContext* m_decoder = nullptr;
		SwsContext* m_scaler = nullptr;
		AVPacket* m_packet = nullptr;
		AVFrame* m_frame = nullptr;

		int m_streamIndex = -1;
		std::vector<Entry> m_table;
		lain::media::FrameSpec m_spec;
		std::string m_container;
		std::string m_codec;

		// Where the decoder is, in ORDINALS, so sequential reads never seek. npos means "nowhere
		// useful" — freshly opened, or a decode that failed partway and must not be trusted to
		// continue from wherever it stopped.
		static constexpr std::size_t nowhere = static_cast<std::size_t>(-1);
		std::size_t m_nextOrdinal = nowhere;
	};
} // namespace lain::io::video::ffmpeg
