#pragma once

#include "lain/io/video/writer.h"

#include <lain/core/factory.h>
#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <memory>
#include <string>
#include <string_view>

namespace lain::io::video
{
	// The writer registry — the process-wide table of VideoWriters, keyed by BACKEND name
	// ("ffmpeg"), for the same reason readerRegistry() is: a muxer claims a family of containers
	// and whatever encoders its build has, not a format. open.h carries the full reasoning; it
	// applies unchanged here, and the write side is where its second half — "the container/codec
	// distinction surfaces as REPORTED FACTS and, at the writer, as OPTIONS" — becomes concrete.
	//
	// registerCodec() fills both registries, so a build that can open an mp4 can also write one —
	// the rule plugins/io/image/png already follows.
	lain::core::Factory<VideoWriter>& writerRegistry();

	// Whether `spec` can be written as video WITHOUT LOSS. Quiet — no logging — because this is the
	// honest pre-check a caller runs before offering an output, exactly as io::image::canEncode is.
	//
	// MEDIUM-WIDE, NOT BACKEND-SPECIFIC, which is why it lives in the seam rather than in a plugin:
	// no video codec in any backend holds an alpha channel, and Linear and Unspecified are not
	// transfers a container can state. A writer must not silently degrade what it is handed (the
	// ImageWriter rule), so this refuses and the caller converts explicitly.
	[[nodiscard]] bool canEncode(const lain::media::FrameSpec& spec);

	// Which axis makes `spec` unwritable, for the log line — empty when it is acceptable.
	//
	// The seam-side mirror of the FFmpeg plugin's refusedTagName, and separate from canEncode for
	// the same reason that pair is: the predicate is asked in bulk and must stay quiet, while a
	// refusal is reported once and must name what to fix. A refusal that did not say which of four
	// axes was wrong would leave a caller guessing.
	[[nodiscard]] std::string refusedSpecReason(const lain::media::FrameSpec& spec);

	// Open `uri` for writing as video: create the transport, choose a backend, hand it the spec.
	//
	// The returned handle owns the file until finish(); NOTHING ON DISK IS VALID BEFORE THAT, which
	// is the property that makes this a handle instead of a function.
	//
	// Returns nullptr when the uri does not name a video container, when the spec is one lain
	// refuses to write, when no writer is registered (a build with no video codec plugin), or when
	// no backend could serve options.codec into this container — the reason, naming what was
	// missing, is logged in every case. Backends are tried in registration order and the first that
	// opens wins, symmetric with open(): a backend with no encoder for the request refuses, and the
	// next one may not.
	//
	// THIS IS THE PREFLIGHT. A caller with 500 frames to render asks the question once, here,
	// before the first frame is encoded — the same discipline io::image::canEncode gives the still
	// path, arrived at by a different route because opening an encoder already answers it.
	[[nodiscard]] std::unique_ptr<VideoWriter> openWriter(std::string_view uri, const lain::media::FrameSpec& spec,
														  const VideoWriterOptions& options = {});

	// Encode every frame of `sequence` to `uri` — the one-shot TRANSCODE facade over the handle
	// (ADR-0018), and the video peer of io::image::save.
	//
	// The spec is the sequence's own, which is a fact rather than a guess: a sequence is
	// homogeneous, so its declared spec IS every frame's. A sequence whose rate is unspecified (a
	// folder of stills) is refused, pointing at io::sequence::open's fallback rate — the knob that
	// already exists for exactly this.
	//
	// An EMPTY sequence is refused rather than producing a zero-frame container: the caller asked
	// to transcode nothing, and a file that hides that is worse than an error.
	//
	// Returns false on an unopenable output, on a frame that failed to decode (a hole, which a
	// video cannot represent), or on a failed finish — and FINISHES THE WRITER EITHER WAY, so a
	// failure leaves a closed, short file rather than a headless one.
	//
	// It is deliberately not the RENDER path: a render's frames come from a graph one at a time and
	// its missing-frame policy is the host's, so a host drives the handle directly (ADR-0018 — "a
	// suppressed frame is the host's policy, not the writer's").
	[[nodiscard]] bool save(std::string_view uri, const lain::media::FrameSequence& sequence,
							const VideoWriterOptions& options = {});
} // namespace lain::io::video
