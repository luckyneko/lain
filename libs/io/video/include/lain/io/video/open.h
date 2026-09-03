#pragma once

#include "lain/io/video/reader.h"

#include <lain/core/factory.h>
#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lain::io::video
{
	// The reader registry — the process-wide table of VideoReaders, keyed by BACKEND NAME
	// ("ffmpeg") rather than by format.
	//
	// BACKEND is the exact word, and video is where it starts to matter. A video file has two
	// aspects: a CONTAINER (mp4, mov, mkv — what muxes the streams) and a CODEC (h264, prores,
	// vp9 — what encodes the frames). "ffmpeg" is neither; it is one implementation covering both,
	// and a platform-native replacement (AVFoundation, Media Foundation) would replace both at
	// once. That is what this registry chooses between.
	//
	// It is therefore the one place this seam deliberately does not mirror io::image. An image
	// codec claims a format — libpng decodes png and nothing else — so keying by extension there is
	// keying by capability. A demuxer claims a whole family and identifies containers by their
	// CONTENT, not their name (FFmpeg opens an mp4 called .bin), so an extension key here would be
	// a second, worse answer to a question the demuxer already answers better. The extension
	// selects the MEDIUM (see videoExtensions and io::sequence); the backend is chosen by being
	// registered.
	//
	// WHY CONTAINER AND CODEC ARE NOT TWO REGISTRIES (settled 2026-09-02; WORK.md M10 records the
	// trigger to revisit). The obvious decomposition — Demuxer -> Packet, Decoder(Packet) -> Image
	// — cuts through the middle of one thing rather than between two. A demuxer's output is not
	// codec-neutral: the same H.264 stream leaves an MP4 as length-prefixed AVCC with its parameter
	// sets in the container's extradata and a TS as in-band Annex-B, which is why FFmpeg needs a
	// bitstream filter to convert one to the other. And "decode frame 412" is ONE algorithm across
	// both halves — seek to the preceding keyframe (demuxer knowledge), flush, decode forward
	// matching by pts (decoder state). Splitting the interface would not decouple that; it would
	// route the coupling through a public boundary and a Packet type lain would then own. The
	// distinction is real, and it surfaces as REPORTED FACTS — a reader naming the container and
	// codec it found (slice 5b, with a log line to consume them) — and, at the writer, as options.
	// Not as structure.
	lain::core::Factory<VideoReader>& readerRegistry();

	// The CONTAINER extensions this seam claims for the video medium — the one list, owned here
	// rather than in a backend plugin. "mp4" names a container, never a codec: what a file is
	// called says how its streams are muxed and nothing about what encoded them.
	//
	// It lives in the SEAM because the diagnostic depends on it: with no plugin built, .mp4 must
	// still route here and be told "this build has no video codec", not fall through to the still
	// opener and be told it is not a directory. A capability is missing, not a vocabulary
	// (ADR-0019, amended). It is a claim about which medium a name belongs to, which is exactly
	// the question that survives having no backend at all.
	const std::vector<std::string>& videoExtensions();

	// Whether `uri` names a video CONTAINER this seam claims — io::extensionKey against
	// videoExtensions(), asked as a predicate.
	//
	// Public for exactly the reason io::image::formatKeyOf is: a render sweep has to decide whether
	// an Image-valued output is a run of numbered stills or ONE video, and deriving that a second
	// way in the app would let the decision and the write disagree about what a path means. Two
	// callers want the QUESTION rather than the list, and a linear scan written twice is a list
	// decided in two places.
	//
	// It asks about the NAME, so it answers the same in a build with no video codec at all — which
	// is what keeps that build's refusal "this build has no video codec plugin" rather than
	// something about extensions.
	//
	// NOTE the read claim and the write CAPABILITY are different sets and always will be: this
	// lists what lain recognises AS video, and an LGPL FFmpeg reads .webm without being able to
	// encode one. openWriter is where that difference is discovered and said out loud.
	[[nodiscard]] bool isVideoUri(std::string_view uri);

	// Open `uri` as a frame sequence of decoded video frames — the video medium's contribution to
	// the frame-sequence model, peer of io::image::openSequence.
	//
	// The transport is opened here (io::openStream) and handed to the reader, so the codec plugin
	// holds a Stream and never a file. Frames are decoded lazily behind the source's lock, with the
	// ring cache media::FrameSource already provides.
	//
	// `rate` is the FALLBACK rate, used only when the container declares none of its own — a raw
	// stream genuinely has no rate. A container that states one keeps it.
	//
	// Returns std::nullopt when the uri cannot be opened, when no video reader is registered (a
	// build without a video codec plugin), or when the reader refuses the container. The reason is
	// logged in every case.
	[[nodiscard]] std::optional<lain::media::FrameSequence> open(std::string_view uri,
																 lain::media::FrameRate rate = {});
} // namespace lain::io::video
