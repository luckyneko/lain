#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace flowview
{
	// The output side of a render: turning a "<frame:04>" pattern into one frame's path.
	//
	// Its own translation unit, like pinkey.h and canvasids.cpp, because it is pure — no graph, no
	// evaluation, no driver — and therefore testable on its own. The rest of the sweep is a loop
	// around a scheduler and can only be tested end to end.
	//
	// The range half of a sweep is not here: that is lain::core::Range, which the cli parses
	// directly through its own lexical_cast rather than through a helper in this app.

	// The output path for one frame: `pattern` with its frame key replaced by `frame`.
	//
	// Names io::image::frameKey rather than a spelling of its own, so what a sweep writes is what
	// io::image::openSequence reads back. Returns nullopt when the pattern's spec is one fmt
	// refuses ("out.<frame:zz>.png") — a caller's text, so the caller reports it.
	[[nodiscard]] std::optional<std::string> frameOutputPath(std::string_view pattern, std::size_t frame);

	// Whether `pattern` can distinguish one frame from another — i.e. whether it names the frame
	// key at all.
	//
	// A sweep writing through a pattern without one would overwrite a single file once per frame
	// and exit reporting success, which is the failure mode ADR-0018 refuses for a truncated render:
	// silently destroying the correspondence between input and output frames.
	[[nodiscard]] bool isFramePattern(std::string_view pattern);
} // namespace flowview
