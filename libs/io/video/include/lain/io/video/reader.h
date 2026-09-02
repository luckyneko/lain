#pragma once

#include <lain/core/time.h>
#include <lain/image/image.h>
#include <lain/io/stream.h>
#include <lain/media/framespec.h>

#include <cstddef>
#include <memory>
#include <string>

namespace lain::io::video
{
	// A VideoReader demuxes and decodes ONE video container — the video medium's peer of
	// ImageReader, and the only thing a video BACKEND plugin has to implement.
	//
	// It spans both aspects of a video file — the CONTAINER that muxes the streams and the CODEC
	// that encoded the frames — because that is where a real implementation is swapped: FFmpeg
	// covers both, and a platform-native replacement would replace both at once. open.h carries
	// the reasoning for not splitting it in two, which is not merely that one library does both.
	//
	// It is handed an open ReadStream rather than a uri: the transport is lain's (ADR-0004), so a
	// backend never opens a file, and a future s3:// scheme reaches the decoder through the same
	// seam. The prebuilt FFmpeg is --disable-network, so for video this is not a nicety.
	//
	// Unlike ImageReader — stateless, one decode per construction — a video reader is STATEFUL and
	// long-lived: it holds the container open, owns its frame table, and its decoder has a
	// position, which is why decode() is non-const and why sequential access is its fast path.
	// One reader serves one source for as long as any evaluation retains the sequence.
	//
	// NOT thread-safe. Its owner (io::video's VideoSource, through media::FrameSource's lock)
	// serialises every call, the same way a Stream's owner does.
	class VideoReader
	{
	public:
		virtual ~VideoReader() = default;

		// Take `stream` and establish the sequence: demux, build the frame table, decide the spec.
		//
		// `fallbackRate` is the rate to declare when the container declares none of its own — the
		// same tolerance media::unify grants the rate axis and no other, since a raw stream
		// genuinely has no rate and every other axis is a fact about the pixels. A container that
		// states a rate keeps it; this is never an override.
		//
		// Returns false when the container cannot be read or its frames cannot be delivered under
		// lain's rules (an explicitly tagged BT.601 / BT.2020 / PQ / HLG transfer is refused rather
		// than relabelled — ADR-0018). The reason is logged; nothing else is called after a false.
		[[nodiscard]] virtual bool open(std::unique_ptr<ReadStream> stream, lain::media::FrameRate fallbackRate) = 0;

		// The declared shape of every frame, established at open and never re-derived.
		virtual const lain::media::FrameSpec& spec() const = 0;

		// What this file actually turned out to be: the CONTAINER that muxed it ("mov,mp4,m4a" —
		// FFmpeg names a demuxer family) and the CODEC that encoded the frames ("h264"). Reported,
		// not dispatched on: this is where the two aspects of a video file surface, since they are
		// deliberately not two registries and two interfaces (see open.h). Established at open;
		// empty when the backend cannot say.
		virtual const std::string& container() const = 0;
		virtual const std::string& codec() const = 0;

		// Exact, because the source is indexed at open rather than estimated from a duration.
		virtual std::size_t frameCount() const = 0;

		// Presentation time of frame `ordinal` from the start of the container, as the container
		// reports it — which is what makes variable frame rate free rather than special.
		virtual lain::core::Time timestamp(std::size_t ordinal) const = 0;

		// Frame `ordinal`, decoded. Non-const: a decoder has a position, and this may seek it.
		// Returns an invalid Image on any failure, exactly as ImageReader::decode does — the
		// caller (a FrameSource) turns that into "this frame produced no value", and the whole
		// engine already reads an invalid Image that way.
		[[nodiscard]] virtual lain::image::Image decode(std::size_t ordinal) = 0;
	};
} // namespace lain::io::video
