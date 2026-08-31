#pragma once

#include "lain/media/framesequence.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace lain::media
{
	// The sequence verbs. Every one is a LIST operation (ADR-0018) — none of them decodes, none
	// of them wraps a source, and all of them preserve frame identity while re-basing position.
	//
	// SELECTION, NOT EDIT, is the governing rule these obey: a frame reference names a source and
	// an ordinal, and a frame a graph COMPUTED has no source. So these can express clip, reorder,
	// concatenate and subset, and structurally cannot carry processed pixels.
	//
	// Note which of them can fail and which cannot, and why. clip / reverse / stride only ever
	// remove or reorder entries that were already unified, so they cannot introduce a spec
	// mismatch and are total. concat and select can: one adds sources, the other names positions
	// that may not exist.

	// `count` frames from `position`, CLAMPED to what exists — clip(seq, 100, 50) on a 120-frame
	// sequence yields 20 frames, rather than refusing. Position 0 of the result is still its
	// source's ordinal 100: a clip re-bases position and preserves identity.
	[[nodiscard]] FrameSequence clip(const FrameSequence& sequence, std::size_t position, std::size_t count);

	// `a` then `b`. nullopt when their specs cannot be unified — the composition point at which
	// homogeneity is enforced, and the reason a heterogeneous sequence cannot be built at all
	// rather than discovered mid-render. Concatenating with an empty sequence yields the other.
	[[nodiscard]] std::optional<FrameSequence> concat(const FrameSequence& a, const FrameSequence& b);

	// Last frame first.
	[[nodiscard]] FrameSequence reverse(const FrameSequence& sequence);

	// Every `step`-th frame from position 0. A step of 1 is the identity; a step of 0 is a caller
	// error and yields an empty sequence with a logged reason, rather than being quietly read as
	// 1 — which would return a sequence that looks right and means something else.
	[[nodiscard]] FrameSequence stride(const FrameSequence& sequence, std::size_t step);

	// The frames at `positions`, in the order given — repeats and reordering allowed. This is
	// what CONTEXT.md calls **calibration view selection**: the thirty views a metric chose out
	// of a longer sequence.
	//
	// nullopt if ANY position is out of range, rather than skipping it. A caller's positions
	// either describe this sequence or they do not, and silently dropping one would corrupt the
	// correspondence between what was chosen and what came back — the same reasoning that makes
	// a map with one suppressed element clear its whole output (ADR-0014).
	[[nodiscard]] std::optional<FrameSequence> select(const FrameSequence& sequence,
													  const std::vector<std::size_t>& positions);
} // namespace lain::media
