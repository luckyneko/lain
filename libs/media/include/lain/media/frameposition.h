#pragma once

#include <cstddef>
#include <string>

namespace lain::media
{
	// Where in a frame sequence to look — the input a host varies to drive a render.
	//
	// A DISTINCT TYPE, not an int (ADR-0018). That is what lets the cli find the loop counter with
	// no naming convention, lets `list` report a graph as renderable, and will let an editor render
	// a timeline BY TYPE. A plain int cannot say "this is a position in a frame sequence", so
	// nothing downstream could act on it, and a pin named `frame` by convention breaks silently the
	// moment a linked group renames its interface.
	//
	// It is a struct rather than an alias for exactly the same reason it is not an int: every
	// registry that makes a payload type first-class — port types, value codecs, cli binders, param
	// editors — is keyed by std::type_index, and `using FramePosition = std::size_t` would be
	// indistinguishable from a plain count in all four at once.
	//
	// Spelled FramePosition rather than FrameIdx because nothing in this codebase abbreviates, and
	// because *index* is a spent word here — see CONTEXT.md's *Position*.
	struct FramePosition
	{
		std::size_t value = 0;

		std::string toString() const; // "frame 12"
	};

	bool operator==(FramePosition a, FramePosition b);
	bool operator!=(FramePosition a, FramePosition b);
} // namespace lain::media
