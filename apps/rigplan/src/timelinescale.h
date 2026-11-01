#pragma once

#include <lain/rigplan/mover.h>
#include <lain/rigplan/rigplan.h>

#include <cstddef>
#include <optional>

namespace rigplan
{
	// Where the frames of a capture timeline stand across the Timeline pane's track, in the pane's points.
	// Each frame has a slot of equal width, its tick at the slot's centre, so the first and the last frame
	// are as easy to reach as any other and a timeline of one frame is one slot the width of the track.
	//
	// A key past the timeline (check() does not report one: a capture simply never reaches it) is drawn in
	// a strip at the right end of the track, `overflow` wide, so it is seen however far past it is, and the
	// timeline is never squeezed to make room for a key a thousand frames on.
	struct TimelineScale
	{
		double left = 0.0;		// where frame 0's slot starts
		double width = 1.0;		// the whole track, the strip included
		std::size_t frames = 1; // the timeline's: frames 0 to frames - 1, and at least one is counted
		double overflow = 0.0;	// the strip's width, 0 when no key is past the timeline

		// One frame's slot: the track less the strip, over the frames.
		double slot() const;
		// The centre of `frame`'s slot, or of the strip for a frame past the timeline.
		double xOf(std::size_t frame) const;
		// The frame whose slot `x` falls in, held within the timeline: the strip, and anything left of the
		// first slot or right of the last, is the nearest end. Never a frame past the timeline, since one
		// is reached only by selecting its key.
		std::size_t frameAt(double x) const;
		bool beyond(std::size_t frame) const { return frame >= counted(); }

	private:
		std::size_t counted() const { return frames == 0 ? 1 : frames; }
	};

	// Whether any mover in `plan` has a key past its timeline, which the Timeline then has a strip for.
	bool keysBeyond(const lain::rigplan::RigPlan& plan);

	// The key of `path` whose tick is nearest `x`, within `radius` of it, or none. On a tie the earlier key,
	// so every key past the timeline, all drawn in the strip, picks the first of them.
	std::optional<std::size_t> nearestKey(const TimelineScale& scale, const lain::rigplan::Path& path, double x, double radius);
} // namespace rigplan
