#include "ffmpegwriter.h"

#include "aviobridge.h"
#include "colorpolicy.h"
#include "encoderpolicy.h"

#include <lain/log/log.h>

#include <string>
#include <utility>
#include <vector>

extern "C"
{
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace lain::io::video::ffmpeg
{
	// --- lifetime ---------------------------------------------------------------

	FFmpegVideoWriter::~FFmpegVideoWriter()
	{
		if (!m_finished)
		{
			// No file is left headless. What an unfinished writer loses is the REPORT, not the
			// bytes — the same contract io::WriteStream states one layer down, and the reason
			// finish() is explicit rather than the destructor being the commit.
			if (!finish())
				lain::log::error("io::video::ffmpeg: an unfinished writer failed to close");
		}
		closeAll();
	}

	void FFmpegVideoWriter::closeAll()
	{
		if (m_scaler != nullptr)
			sws_freeContext(m_scaler);
		m_scaler = nullptr;

		av_frame_free(&m_frame);
		av_packet_free(&m_packet);
		avcodec_free_context(&m_encoder);

		if (m_format != nullptr)
		{
			// The pb is OURS, not FFmpeg's: avformat_free_context would not touch it, but nulling
			// it first makes that explicit rather than depending on which of the two owns what.
			m_format->pb = nullptr;
			avformat_free_context(m_format);
			m_format = nullptr;
		}
		m_videoStream = nullptr;

		freeContext(m_avio);
	}

	// --- encoder selection ------------------------------------------------------

	AVPixelFormat FFmpegVideoWriter::choosePixelFormat(const AVCodec& encoder, bool lossless)
	{
		const AVPixelFormat* formats = nullptr;
		int count = 0;
		if (avcodec_get_supported_config(nullptr, &encoder, AV_CODEC_CONFIG_PIX_FORMAT, 0,
										 reinterpret_cast<const void**>(&formats), &count) < 0 ||
			formats == nullptr || count == 0)
		{
			// An encoder that will not say takes what it is given; yuv420p is the safe assumption
			// and the one every delivery encoder accepts.
			return AV_PIX_FMT_YUV420P;
		}

		if (lossless)
		{
			// PREFER RGB, and this is not a preference — it is what makes the word lossless true.
			// An 8-bit RGB->YUV matrix is not invertible, so FFV1 over yuv420p would be a lossless
			// encoding of a lossy conversion: the file round-trips and the FRAMES do not.
			for (int i = 0; i < count; ++i)
			{
				const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(formats[i]);
				if (descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0 && descriptor->comp[0].depth == 8)
					return formats[i];
			}
		}

		for (int i = 0; i < count; ++i)
		{
			if (formats[i] == AV_PIX_FMT_YUV420P)
				return formats[i]; // what every player decodes
		}
		return formats[0];
	}

	bool FFmpegVideoWriter::tryEncoder(const std::string& name, const AVOutputFormat* oformat,
									   const VideoWriterOptions& options)
	{
		const AVCodec* encoder = avcodec_find_encoder_by_name(name.c_str());
		if (encoder == nullptr)
		{
			lain::log::info("io::video::ffmpeg: {} is not in this build", name);
			return false;
		}

		if (avformat_query_codec(oformat, encoder->id, FF_COMPLIANCE_NORMAL) <= 0)
		{
			// Container/codec compatibility folded into the same availability question, so a
			// mismatch is reported here rather than surfacing later and obscurely at write_header.
			lain::log::info("io::video::ffmpeg: the {} container will not carry {}", oformat->name, name);
			return false;
		}

		m_encoder = avcodec_alloc_context3(encoder);
		if (m_encoder == nullptr)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating the encoder context");
			return false;
		}

		m_pixelFormat = choosePixelFormat(*encoder, isLossless(options.codec));

		m_encoder->width = m_spec.extent.x;
		m_encoder->height = m_spec.extent.y;
		m_encoder->pix_fmt = m_pixelFormat;

		// The EXACT rational, reciprocated — never through FrameRate::hz(). A rate that has been
		// through a double cannot be written back into a container's timebase without drifting,
		// which is the reason FrameRate is a rational in the first place.
		m_encoder->time_base = AVRational{static_cast<int>(m_spec.rate.denominator),
										  static_cast<int>(m_spec.rate.numerator)};
		m_encoder->framerate = AVRational{static_cast<int>(m_spec.rate.numerator),
										  static_cast<int>(m_spec.rate.denominator)};

		const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(m_pixelFormat);
		const bool rgb = descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;

		const std::optional<ColorTags> tags = colorTagsFor(m_spec.colorSpace, rgb);
		if (!tags)
		{
			// Unreachable: io::video::canEncode refused this spec at the seam. Handled rather than
			// asserted, because a silently untagged file is exactly what the colour rule exists to
			// prevent and this is the last place to notice.
			lain::log::error("io::video::ffmpeg: no container tags for the requested colour space");
			avcodec_free_context(&m_encoder);
			return false;
		}
		m_encoder->color_trc = tags->transfer;
		m_encoder->color_primaries = tags->primaries;
		m_encoder->colorspace = tags->matrix;
		// RGB is always full range; a YUV stream lain writes is limited range, which is the
		// convention a player assumes and what the range compression in write() targets.
		m_encoder->color_range = rgb ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

		if (options.bitsPerSecond > 0)
		{
			m_encoder->bit_rate = options.bitsPerSecond;
		}
		else if (!isLossless(options.codec))
		{
			// Derived rather than left at FFmpeg's zero, which is either a refusal to open or a
			// fixed low number nobody chose. ~0.15 bits per pixel per frame, which is about
			// 8 Mbit/s at 1080p24 — a defensible delivery default, said out loud instead of hidden.
			const double pixelsPerSecond = static_cast<double>(m_spec.extent.x) * m_spec.extent.y * m_spec.rate.hz();
			m_encoder->bit_rate = static_cast<std::int64_t>(std::max(200000.0, pixelsPerSecond * 0.15));
		}

		// GLOBAL HEADER — the one-line difference between a playable MP4 and a file that decodes to
		// nothing. MP4/MOV keep the parameter sets in the container's extradata rather than in the
		// bitstream, so an encoder not told to emit them out-of-band produces a stream with no
		// SPS/PPS anywhere the demuxer will look.
		if ((oformat->flags & AVFMT_GLOBALHEADER) != 0)
			m_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		// THE ONLY AVAILABILITY TEST THAT IS TRUE. An encoder can be present in a build and fail
		// here for want of a device — NVENC on a machine with no NVIDIA card — which is information
		// about this machine, not a fault, hence info rather than error.
		if (avcodec_open2(m_encoder, encoder, nullptr) < 0)
		{
			lain::log::info("io::video::ffmpeg: {} is in this build but would not open here", name);
			avcodec_free_context(&m_encoder);
			return false;
		}

		m_codec = encoder->name;
		return true;
	}

	// --- open -------------------------------------------------------------------

	bool FFmpegVideoWriter::open(std::unique_ptr<lain::io::WriteStream> stream, std::string_view format,
								 const lain::media::FrameSpec& spec, const VideoWriterOptions& options)
	{
		m_stream = std::move(stream);
		if (!m_stream)
			return false;
		m_spec = spec;

		// FFmpeg already owns extension -> muxer, and it knows things a second table in lain would
		// eventually disagree with the demuxer about (.mkv is matroska, .m4v is mp4). A name is
		// synthesised rather than passing the uri: the plugin is given a format claim, never a path
		// it could open (ADR-0004).
		const std::string filename = "x." + std::string{format};
		const AVOutputFormat* oformat = av_guess_format(nullptr, filename.c_str(), nullptr);
		if (oformat == nullptr)
		{
			lain::log::error("io::video::ffmpeg: no muxer in this build writes .{}", format);
			return false;
		}
		m_container = oformat->name;

		const std::vector<std::string> candidates = encoderCandidates(options.codec);
		for (const std::string& candidate : candidates)
		{
			if (tryEncoder(candidate, oformat, options))
				break;
		}

		if (m_encoder == nullptr)
		{
			// NAMING WHAT IS MISSING, not shrugging (ADR-0019). Each candidate already logged why
			// it could not serve; this says what the caller has to change, which is why it also
			// reports the families this build CAN provide for this container.
			std::string available;
			for (const VideoCodec family : {VideoCodec::H264, VideoCodec::HEVC, VideoCodec::ProRes,
											VideoCodec::FFV1, VideoCodec::MJPEG})
			{
				for (const std::string& candidate : encoderCandidates(family))
				{
					const AVCodec* encoder = avcodec_find_encoder_by_name(candidate.c_str());
					if (encoder != nullptr && avformat_query_codec(oformat, encoder->id, FF_COMPLIANCE_NORMAL) > 0)
					{
						available += (available.empty() ? "" : ", ") + codecName(family);
						break;
					}
				}
			}

			lain::log::error("io::video::ffmpeg: no {} encoder is available for the {} container; "
							 "this build offers {} here",
							 codecName(options.codec), m_container,
							 available.empty() ? std::string{"nothing"} : available);
			return false;
		}

		m_avio = makeWriteContext(*m_stream);
		if (m_avio == nullptr)
			return false; // makeWriteContext logged the reason

		if (avformat_alloc_output_context2(&m_format, oformat, nullptr, nullptr) < 0 || m_format == nullptr)
		{
			lain::log::error("io::video::ffmpeg: could not create the {} muxer", m_container);
			return false;
		}
		m_format->pb = m_avio;
		m_format->flags |= AVFMT_FLAG_CUSTOM_IO;

		m_videoStream = avformat_new_stream(m_format, nullptr);
		if (m_videoStream == nullptr)
		{
			lain::log::error("io::video::ffmpeg: could not add a video stream to the {} muxer", m_container);
			return false;
		}
		if (avcodec_parameters_from_context(m_videoStream->codecpar, m_encoder) < 0)
		{
			lain::log::error("io::video::ffmpeg: could not describe the stream to the {} muxer", m_container);
			return false;
		}
		m_videoStream->time_base = m_encoder->time_base;

		m_packet = av_packet_alloc();
		m_frame = av_frame_alloc();
		if (m_packet == nullptr || m_frame == nullptr)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating the encode buffers");
			return false;
		}
		m_frame->format = m_pixelFormat;
		m_frame->width = m_spec.extent.x;
		m_frame->height = m_spec.extent.y;
		if (av_frame_get_buffer(m_frame, 0) < 0)
		{
			lain::log::error("io::video::ffmpeg: out of memory allocating the encode frame");
			return false;
		}

		// The muxer writes a header here and SEEKS BACK over it at the trailer — an MP4 patches its
		// mdat size and appends its moov once the frames are known. That is the caller slice 3's
		// positional, non-truncating WriteStream was specified for.
		if (avformat_write_header(m_format, nullptr) < 0)
		{
			lain::log::error("io::video::ffmpeg: could not write the {} header", m_container);
			return false;
		}
		m_headerWritten = true;
		return true;
	}

	// --- write ------------------------------------------------------------------

	bool FFmpegVideoWriter::encodeAndMux(AVFrame* frame)
	{
		if (avcodec_send_frame(m_encoder, frame) < 0)
			return false;

		for (;;)
		{
			const int got = avcodec_receive_packet(m_encoder, m_packet);
			if (got == AVERROR(EAGAIN) || got == AVERROR_EOF)
				return true;
			if (got < 0)
				return false;

			av_packet_rescale_ts(m_packet, m_encoder->time_base, m_videoStream->time_base);
			m_packet->stream_index = m_videoStream->index;

			const int written = av_interleaved_write_frame(m_format, m_packet);
			av_packet_unref(m_packet);
			if (written < 0)
				return false;
		}
	}

	bool FFmpegVideoWriter::write(const lain::image::Image& image)
	{
		if (m_encoder == nullptr || !m_headerWritten)
			return false;

		// The homogeneity question, asked through the one function that owns it, so a writer
		// validating its frames and a composition validating two specs cannot drift apart.
		// Refused, never rescaled or relabelled (ADR-0018).
		if (!lain::media::matches(m_spec, image))
		{
			lain::log::error("io::video::ffmpeg: frame {} is {}, which does not match the declared {}", m_written,
							 image.toString(), m_spec.toString());
			return false;
		}

		// lain's packed pixels as FFmpeg names them. The seam has already refused everything but
		// 8-bit Gray and RGB, so these two arms are the whole mapping — the mirror of the reader
		// converting everything down to RGB24 on the way in.
		const AVPixelFormat sourceFormat =
			m_spec.pixelFormat == lain::image::PixelFormat::Gray8 ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_RGB24;
		const int sourceStride =
			m_spec.extent.x * static_cast<int>(lain::image::descriptor(m_spec.pixelFormat).bytesPerPixel());

		m_scaler = sws_getCachedContext(m_scaler, m_spec.extent.x, m_spec.extent.y, sourceFormat,
										m_spec.extent.x, m_spec.extent.y, m_pixelFormat, SWS_BILINEAR, nullptr,
										nullptr, nullptr);
		if (m_scaler == nullptr)
		{
			lain::log::error("io::video::ffmpeg: could not create the {} to {} converter",
							 av_get_pix_fmt_name(sourceFormat), av_get_pix_fmt_name(m_pixelFormat));
			return false;
		}

		// RANGE COMPRESSION — the exact mirror of the reader's expansion. RGB is always full range
		// and a YUV video stream is conventionally limited (16-235), so a full-range frame written
		// into a limited-range-tagged stream without this comes back crushed: swscale's default for
		// an unspecified destination is not the one the tag claims.
		const int destinationRange = m_encoder->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
		const int* sourceMatrix = sws_getCoefficients(SWS_CS_DEFAULT);
		const int* destinationMatrix = sws_getCoefficients(m_encoder->colorspace);
		(void)sws_setColorspaceDetails(m_scaler, sourceMatrix, 1, destinationMatrix, destinationRange, 0, 1 << 16,
									   1 << 16);

		if (av_frame_make_writable(m_frame) < 0)
		{
			lain::log::error("io::video::ffmpeg: could not make the encode frame writable");
			return false;
		}

		const std::uint8_t* planes[4] = {image.data(), nullptr, nullptr, nullptr};
		const int strides[4] = {sourceStride, 0, 0, 0};
		const int rows = sws_scale(m_scaler, planes, strides, 0, m_spec.extent.y, m_frame->data, m_frame->linesize);
		if (rows != m_spec.extent.y)
		{
			lain::log::error("io::video::ffmpeg: converted {} of {} rows of frame {}", rows, m_spec.extent.y,
							 m_written);
			return false;
		}

		// Position IS the timestamp, because the timebase is the reciprocal of the rate. That is
		// what makes the output constant-rate and exactly as long as the frames written.
		m_frame->pts = m_written;

		if (!encodeAndMux(m_frame))
		{
			lain::log::error("io::video::ffmpeg: failed to encode frame {}", m_written);
			return false;
		}

		++m_written;
		return true;
	}

	// --- finish -----------------------------------------------------------------

	bool FFmpegVideoWriter::finish()
	{
		if (m_finished)
			return m_finishStatus; // a second trailer would corrupt the file it is meant to close

		m_finished = true;
		m_finishStatus = true;

		if (m_headerWritten)
		{
			// Drain: an encoder holds frames back (B-frames, lookahead), and a null frame is what
			// tells it there are no more. Then the trailer, which is what makes the container
			// valid — and it runs even after a failed write, so a truncated render leaves a
			// PLAYABLE short file rather than a headless one (ADR-0018's stop policy).
			if (!encodeAndMux(nullptr))
			{
				lain::log::error("io::video::ffmpeg: failed to flush the encoder for {}", m_stream->uri());
				m_finishStatus = false;
			}
			if (av_write_trailer(m_format) < 0)
			{
				lain::log::error("io::video::ffmpeg: failed to write the {} trailer", m_container);
				m_finishStatus = false;
			}
			avio_flush(m_avio);
		}

		if (m_stream && !m_stream->finish())
			m_finishStatus = false;

		return m_finishStatus;
	}
} // namespace lain::io::video::ffmpeg
