#pragma once

#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lain::io::sequence
{
	// Open `uri` as a frame sequence, whatever medium it turns out to be — the medium-neutral
	// entry point a graph calls, so a document does not change node vocabulary when its footage
	// goes from a folder of stills to an mp4.
	//
	// It lives here, one level up from the per-medium seams, because lain::media depends on no io
	// at all: a medium-neutral library that knew how to open things would end up depending on every
	// medium, which is the junk drawer CONTEXT.md's loading split exists to prevent. Each medium's
	// opener stays in its own seam (io::image::openSequence, io::video::open) and this dispatches
	// to them.
	//
	// Namespace note: `lain::io::sequence`, not `lain::io::media`, because inside the latter the
	// name `media` would resolve to itself and shadow `lain::media` at every mention.
	//
	// DISPATCH IS BY EXTENSION, WITH ONE DEFAULT. io::extensionKey(uri) selects a registered
	// opener; anything unclaimed — a directory, a "shot.####.png" pattern, a still — goes to the
	// default. That asymmetry is not a shortcut: the video medium is addressed by what a FILE is
	// called, while the image medium is addressed STRUCTURALLY (a folder, or a numbered pattern),
	// and a folder has no extension at all to key on.
	//
	// `rate` is the nominal frame rate to declare for a medium that has none of its own (stills,
	// a raw stream). Returns std::nullopt when nothing can open the uri; the reason is logged.
	[[nodiscard]] std::optional<lain::media::FrameSequence> open(std::string_view uri,
																 lain::media::FrameRate rate = {});

	// What an opener is: a uri and a fallback rate in, a sequence or a logged refusal out — the
	// signature both media seams already have.
	using Opener = std::function<std::optional<lain::media::FrameSequence>(std::string_view uri,
																		   lain::media::FrameRate rate)>;

	// Claim `extension` (lowercase, no dot) for `opener`, replacing any previous claim.
	void registerOpener(std::string extension, Opener opener);

	// Install the opener for every uri no extension claims — the still-sequence opener, which
	// answers structurally rather than by name. There is exactly one; registering a second
	// replaces it.
	void registerDefaultOpener(Opener opener);
} // namespace lain::io::sequence
