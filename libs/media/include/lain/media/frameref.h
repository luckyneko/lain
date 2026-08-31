#pragma once

#include <lain/core/time.h>

#include <cstddef>
#include <string>

namespace lain::media
{
	// Which frame of which source a frame IS — its identity, as distinct from where it currently
	// sits (its position). A clip re-bases position and preserves identity: frame 0 of
	// clip(seq, 100, 50) is position 0 and still ordinal 100 of its source. Anything that
	// REPORTS a frame — an issue, a calibration report, a manifest — names the identity;
	// anything ordinal uses the position.
	//
	// A NAME YOU CAN LOOK UP, never a back-pointer (ADR-0018). A reference holding a pointer to
	// its source would keep a decoder, its handle and its cache alive for as long as any
	// evaluation retained any single decoded frame — and flow retains values per node, per
	// element, across runs.
	//
	// The source is a canonical uri rather than a minted id because a minted id changes every
	// run, which would make a saved manifest meaningless. One resource has ONE name however it is
	// spelled, and io::canonicalUri is the single function that decides that — a key computed two
	// ways eventually disagrees with itself, silently.
	struct FrameRef
	{
		// The canonical uri of the source this frame belongs to — a video file, or an
		// image-sequence pattern. Not the individual still's path: the source is the sequence,
		// and the ordinal locates the frame within it.
		std::string source;

		// Which frame OF THAT SOURCE, from 0. Survives clipping, concatenation and selection.
		std::size_t ordinal = 0;

		// Presentation time measured from the start of its source — an offset, not wall clock,
		// which is exactly what core::Time models. Zero when the source declares no timing.
		lain::core::Time timestamp{};

		// A frame reference names a source; one that names none refers to nothing.
		bool valid() const { return !source.empty(); }

		std::string toString() const; // "frame 412 of /footage/take1.mp4"
	};

	bool operator==(const FrameRef& a, const FrameRef& b);
	bool operator!=(const FrameRef& a, const FrameRef& b);
} // namespace lain::media
