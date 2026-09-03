#include "ffmpegreader.h"

#include "aviobridge.h"
#include "colorpolicy.h"

#include <lain/log/log.h>

#include <algorithm>
#include <utility>

extern "C"
{
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace lain::io::video::ffmpeg
{
	// --- lifetime ---------------------------------------------------------------

	FFmpegVideoReader::~FFmpegVideoReader()
	{
		closeAll();
	}

	void FFmpegVideoReader::closeAll()
	{
		if (m_scaler != nullptr)
			sws_freeContext(m_scaler);
		m_scaler = nullptr;

		av_frame_free(&m_frame);
		av_packet_free(&m_packet);
		avcodec_free_context(&m_decoder);

		if (m_format != nullptr)
			avformat_close_input(&m_format);

		freeContext(m_avio);
	}

	// --- open -------------------------------------------------------------------

	bool FFmpegVideoReader::open(std::unique_ptr<lain::io::ReadStream> stream, lain::media::FrameRate fallbackRate)
	{
		m_stream = std::move(stream);
		if (!m_stream)
			return false;

		m_avio = makeReadContext(*m_stream);
		if (m_avio == nullptr)
			return false; // makeReadContext logged the reason

		m_format = avformat_alloc_context();
		if (m_format == nullptr)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating the format context");
			return false;
		}
		m_format->pb = m_avio;
		m_format->flags |= AVFMT_FLAG_CUSTOM_IO;

		if (avformat_open_input(&m_format, nullptr, nullptr, nullptr) < 0)
		{
			// avformat_open_input frees the context on failure and leaves the pointer null, so
			// there is nothing left for closeAll to release beyond the avio context.
			lain::log::error("io::video::ffmpeg: {} is not a container this build can demux",
							 m_stream->uri());
			return false;
		}

		if (avformat_find_stream_info(m_format, nullptr) < 0)
		{
			lain::log::error("io::video::ffmpeg: no stream information in {}", m_stream->uri());
			return false;
		}

		const AVCodec* decoder = nullptr;
		m_streamIndex = av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
		if (m_streamIndex < 0 || decoder == nullptr)
		{
			lain::log::error("io::video::ffmpeg: {} holds no video stream this build can decode",
							 m_stream->uri());
			return false;
		}

		const AVStream& stream_ = *m_format->streams[m_streamIndex];
		const AVCodecParameters& parameters = *stream_.codecpar;

		// The colour rule runs BEFORE anything is decoded: refusing at open is what stops a
		// mislabelled frame ever reaching a graph, and a sequence that cannot be delivered honestly
		// should never have been opened (ADR-0018).
		const std::optional<lain::image::ColorSpace> colorSpace =
			colorSpaceFor(parameters.color_trc, parameters.color_primaries, parameters.color_space);
		if (!colorSpace)
		{
			lain::log::error("io::video::ffmpeg: refusing {} — {} is not a colour space lain can "
							 "process, and relabelling it would be a silent wrong answer (ADR-0018)",
							 m_stream->uri(),
							 refusedTagName(parameters.color_trc, parameters.color_primaries,
											parameters.color_space));
			return false;
		}
		if (parameters.color_trc == AVCOL_TRC_UNSPECIFIED && parameters.color_space == AVCOL_SPC_UNSPECIFIED)
		{
			// Said out loud rather than assumed quietly: it IS a guess, and it is the right one for
			// everything modern (FFmpeg's own convention would guess BT.601 by frame size).
			lain::log::info("io::video::ffmpeg: {} carries no colour tags — treating it as BT709",
							m_stream->uri());
		}

		if (!startDecoder(decoder, &parameters))
			return false;
		if (!buildFrameTable())
			return false;

		m_container = m_format->iformat != nullptr && m_format->iformat->name != nullptr
						  ? m_format->iformat->name
						  : std::string{};
		m_codec = decoder->name != nullptr ? decoder->name : std::string{};

		const AVRational guessed = av_guess_frame_rate(m_format, m_format->streams[m_streamIndex], nullptr);

		m_spec.extent = {parameters.width, parameters.height};
		// RGB8 is the plugin boundary: image::PixelFormat has no planar or subsampled model, so a
		// YUV frame has no honest representation upstream of swscale (WORK.md M10 slice 5).
		m_spec.pixelFormat = lain::image::PixelFormat::RGB8;
		m_spec.colorSpace = *colorSpace;
		m_spec.alphaMode = lain::image::AlphaMode::Unspecified; // RGB8 carries no alpha
		// Kept as the exact rational the container states — 30000/1001, never 29.97 — because a
		// rate that has been through a double cannot be written back into a timebase without drift.
		m_spec.rate = guessed.num > 0 && guessed.den > 0
						  ? lain::media::FrameRate{static_cast<std::uint32_t>(guessed.num),
												   static_cast<std::uint32_t>(guessed.den)}
						  : fallbackRate;

		if (!m_spec.valid())
		{
			lain::log::error("io::video::ffmpeg: {} declares an empty frame size", m_stream->uri());
			return false;
		}
		return true;
	}

	bool FFmpegVideoReader::startDecoder(const AVCodec* decoder, const AVCodecParameters* parameters)
	{
		m_decoder = avcodec_alloc_context3(decoder);
		if (m_decoder == nullptr)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating the decoder");
			return false;
		}
		if (avcodec_parameters_to_context(m_decoder, parameters) < 0)
		{
			lain::log::error("io::video::ffmpeg: could not configure the {} decoder", decoder->name);
			return false;
		}
		if (avcodec_open2(m_decoder, decoder, nullptr) < 0)
		{
			lain::log::error("io::video::ffmpeg: could not open the {} decoder", decoder->name);
			return false;
		}

		m_packet = av_packet_alloc();
		m_frame = av_frame_alloc();
		if (m_packet == nullptr || m_frame == nullptr)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating decode buffers");
			return false;
		}
		return true;
	}

	// --- the frame table --------------------------------------------------------

	bool FFmpegVideoReader::buildFrameTable()
	{
		// ONE DECODE-FREE PASS over the packets. The container's own index is not an alternative:
		// it holds KEYFRAME entries only, so it can answer "where do I start decoding" but never
		// "what is frame 412", which is the question this whole design rests on. The cost is a
		// sequential read of the container's packet headers at open; nothing is decoded here.
		while (av_read_frame(m_format, m_packet) >= 0)
		{
			if (m_packet->stream_index == m_streamIndex)
			{
				// dts as the fallback: a container that stores no presentation timestamps still
				// orders its packets, and for material without B-frames the two agree exactly.
				const std::int64_t pts = m_packet->pts != AV_NOPTS_VALUE ? m_packet->pts : m_packet->dts;
				m_table.push_back(Entry{pts, m_packet->pos, (m_packet->flags & AV_PKT_FLAG_KEY) != 0});
			}
			av_packet_unref(m_packet);
		}

		if (m_table.empty())
		{
			// A container whose video stream holds no packets is broken, not empty. The
			// missing-vs-empty distinction that makes an empty FOLDER a value does not carry over:
			// a folder can legitimately hold nothing, a video file cannot.
			lain::log::error("io::video::ffmpeg: {} holds no video frames", m_stream->uri());
			return false;
		}

		// DEMUX ORDER IS NOT DISPLAY ORDER. With B-frames the packets arrive pts 0, 1536, 512,
		// 1024, ... so a table left in arrival order would hand back a different IMAGE than the one
		// asked for — a wrong answer, not an error. Stable, so material with no timestamps at all
		// keeps the only order it has.
		std::stable_sort(m_table.begin(), m_table.end(),
						 [](const Entry& a, const Entry& b)
						 { return a.pts < b.pts; });

		// Rewind the demuxer: the scan consumed the file, and the first decode should not have to
		// discover that.
		if (av_seek_frame(m_format, m_streamIndex, m_table.front().pts, AVSEEK_FLAG_BACKWARD) >= 0)
		{
			avcodec_flush_buffers(m_decoder);
			m_nextOrdinal = 0;
		}
		return true;
	}

	lain::core::Time FFmpegVideoReader::timestamp(std::size_t ordinal) const
	{
		if (ordinal >= m_table.size())
			return {};

		// What the CONTAINER says, not rate x ordinal — which is what makes variable frame rate
		// free rather than a special case: the frames stay ordinal and only the timestamps are
		// irregular. Rebased on the first frame so a clip that starts at a non-zero pts still
		// reports time from its own start.
		const AVRational base = m_format->streams[m_streamIndex]->time_base;
		const std::int64_t ticks = m_table[ordinal].pts - m_table.front().pts;
		return lain::core::Time::from<lain::core::Time::Seconds>(static_cast<double>(ticks) * av_q2d(base));
	}

	// --- decode -----------------------------------------------------------------

	lain::image::Image FFmpegVideoReader::decode(std::size_t ordinal)
	{
		if (ordinal >= m_table.size())
			return {};

		// Sequential is the fast path, and it is the path the whole design is built around: a
		// render binds one frame position after another, so the common case must not seek.
		if (ordinal != m_nextOrdinal && !seekTo(ordinal))
			return {};

		lain::image::Image image = decodeNextMatching(m_table[ordinal].pts);
		// A failed decode leaves the decoder somewhere unknown; saying so costs one seek on the
		// next call and saves handing back a frame from the wrong place.
		m_nextOrdinal = image.valid() ? ordinal + 1 : nowhere;
		return image;
	}

	bool FFmpegVideoReader::seekTo(std::size_t ordinal)
	{
		// Back to the nearest keyframe at or before the target, then forward: an inter-coded frame
		// cannot be decoded on its own. This is the one thing the container's index IS good for.
		std::size_t keyframe = 0;
		for (std::size_t i = ordinal + 1; i-- > 0;)
		{
			if (m_table[i].keyframe)
			{
				keyframe = i;
				break;
			}
		}

		if (av_seek_frame(m_format, m_streamIndex, m_table[keyframe].pts, AVSEEK_FLAG_BACKWARD) < 0)
		{
			lain::log::error("io::video::ffmpeg: could not seek {} to frame {}", m_stream->uri(), ordinal);
			m_nextOrdinal = nowhere;
			return false;
		}
		avcodec_flush_buffers(m_decoder);
		return true;
	}

	lain::image::Image FFmpegVideoReader::decodeNextMatching(std::int64_t targetPts)
	{
		// Decode forward until the frame with the target timestamp arrives. Matching by PTS rather
		// than by counting is what makes this correct after a seek: the decoder may hand back
		// frames from before the target (a seek lands on a keyframe), and with B-frames it hands
		// them back in display order regardless of how they were stored.
		while (true)
		{
			const int received = avcodec_receive_frame(m_decoder, m_frame);
			if (received == 0)
			{
				const std::int64_t pts = m_frame->best_effort_timestamp != AV_NOPTS_VALUE
											 ? m_frame->best_effort_timestamp
											 : m_frame->pts;
				if (pts >= targetPts)
				{
					lain::image::Image image = convert(*m_frame);
					av_frame_unref(m_frame);
					return image;
				}
				av_frame_unref(m_frame);
				continue;
			}
			if (received != AVERROR(EAGAIN) && received != AVERROR_EOF)
			{
				lain::log::error("io::video::ffmpeg: decode failed in {}", m_stream->uri());
				return {};
			}
			if (received == AVERROR_EOF)
				return {}; // drained without reaching the target: the table and the file disagree

			// The decoder wants more input.
			bool sent = false;
			while (!sent)
			{
				const int read = av_read_frame(m_format, m_packet);
				if (read < 0)
				{
					// End of file: flush the decoder so any frames it is still holding come out.
					if (avcodec_send_packet(m_decoder, nullptr) < 0)
						return {};
					sent = true;
					break;
				}
				if (m_packet->stream_index == m_streamIndex)
				{
					const int status = avcodec_send_packet(m_decoder, m_packet);
					av_packet_unref(m_packet);
					if (status < 0)
					{
						lain::log::error("io::video::ffmpeg: rejected a packet of {}", m_stream->uri());
						return {};
					}
					sent = true;
				}
				else
				{
					av_packet_unref(m_packet);
				}
			}
		}
	}

	lain::image::Image FFmpegVideoReader::convert(const AVFrame& frame)
	{
		const int width = frame.width;
		const int height = frame.height;
		if (width <= 0 || height <= 0)
			return {};

		m_scaler = sws_getCachedContext(m_scaler, width, height, static_cast<AVPixelFormat>(frame.format), width,
										height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (m_scaler == nullptr)
		{
			lain::log::error("io::video::ffmpeg: no conversion from {} to RGB8",
							 av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame.format)));
			return {};
		}

		// RANGE EXPANSION, stated rather than inherited. Video is usually limited-range (16-235),
		// RGB is always full-range, and swscale's default for an unspecified source is limited —
		// so a frame that really was full-range would come back crushed unless its own tag is
		// passed through here.
		const int sourceRange = frame.color_range == AVCOL_RANGE_JPEG ? 1 : 0;
		const int* sourceMatrix = sws_getCoefficients(frame.colorspace);
		const int* destinationMatrix = sws_getCoefficients(SWS_CS_DEFAULT);
		// It fails only on a context whose formats are not both convertible, which cannot be true
		// of one sws_getCachedContext just returned for these formats.
		(void)sws_setColorspaceDetails(m_scaler, sourceMatrix, sourceRange, destinationMatrix, 1, 0, 1 << 16,
									   1 << 16);

		lain::image::Image image{width, height, lain::image::PixelFormat::RGB8, m_spec.colorSpace,
								 lain::image::AlphaMode::Unspecified};

		// The Image is tightly packed, so its stride IS width*3 and swscale can write straight into
		// it — no intermediate buffer, no row-by-row copy.
		std::uint8_t* planes[4] = {image.data(), nullptr, nullptr, nullptr};
		int strides[4] = {width * 3, 0, 0, 0};
		const int written = sws_scale(m_scaler, frame.data, frame.linesize, 0, height, planes, strides);
		if (written != height)
		{
			lain::log::error("io::video::ffmpeg: converted {} of {} rows", written, height);
			return {};
		}
		return image;
	}
} // namespace lain::io::video::ffmpeg
