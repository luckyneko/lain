#pragma once

#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <optional>
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
	// opener stays in its own seam (io::image::openSequence, later io::video::open) and this
	// dispatches to them.
	//
	// Namespace note: `lain::io::sequence`, not `lain::io::media`, because inside the latter the
	// name `media` would resolve to itself and shadow `lain::media` at every mention.
	//
	// TODAY THIS IS ONE MEDIUM. The body is a direct call through to io::image::openSequence, and
	// becomes an extension-keyed registry in M10 slice 5 when video supplies the second registrant
	// — a registry with one member has no choice to make yet, and this keeps every call site
	// unchanged when it grows one. See WORK.md M10.
	//
	// `rate` is the nominal frame rate to declare for a medium that has none of its own (stills).
	// Returns std::nullopt when nothing can open the uri; the reason is logged.
	[[nodiscard]] std::optional<lain::media::FrameSequence> open(std::string_view uri,
																 lain::media::FrameRate rate = {});
} // namespace lain::io::sequence
