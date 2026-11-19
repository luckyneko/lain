// The Timeline's scale (M15 slice 9b): every frame has a slot of one width, so a frame's tick and a click
// in its slot name the same frame, the first and the last as reachably as any; a key past the timeline is
// drawn in a strip of its own; and a click picks the nearest tick.

#include "editorplan.h"
#include "timelinescale.h"

#include <lain/core/uri.h>
#include <lain/rigplan/mover.h>
#include <lain/rigplan/rigplan.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>

namespace rp = lain::rigplan;
using rigplan::TimelineScale;

static TimelineScale scaleOf(std::size_t frames, double overflow = 0.0)
{
	TimelineScale scale;
	scale.left = 50.0;
	scale.width = 800.0;
	scale.frames = frames;
	scale.overflow = overflow;
	return scale;
}

TEST_CASE("a frame's tick and a click in its slot name the same frame, the first and the last as any", "[rigplan][timeline]")
{
	for (const std::size_t frames : {std::size_t(1), std::size_t(2), std::size_t(24), std::size_t(1000), std::size_t(250000)})
	{
		for (const double overflow : {0.0, 28.0})
		{
			INFO(frames << " frames, a strip of " << overflow);
			const TimelineScale scale = scaleOf(frames, overflow);
			const std::size_t last = frames - 1;
			const double s = scale.slot();
			CHECK(s == (scale.width - overflow) / double(frames));
			for (const std::size_t f : {std::size_t(0), std::size_t(1) % frames, frames / 2, last})
			{
				INFO("frame " << f);
				CHECK(scale.frameAt(scale.xOf(f)) == f);
				// Its whole slot is it: from just past its left edge to just short of its right one.
				CHECK(scale.frameAt(scale.left + double(f) * s + s * 0.01) == f);
				CHECK(scale.frameAt(scale.left + double(f + 1) * s - s * 0.01) == f);
			}
			// The ends: the last frame's slot runs up to the strip, and past either end is that end.
			CHECK(scale.frameAt(scale.left + scale.width - overflow - s * 0.01) == last);
			CHECK(scale.frameAt(scale.left) == 0);
			CHECK(scale.frameAt(scale.left - 100.0) == 0);
			CHECK(scale.frameAt(scale.left + scale.width + 100.0) == last);
			// The strip is no frame: a click in it is the last frame, never one past it.
			CHECK(scale.frameAt(scale.left + scale.width - overflow * 0.5) == last);
		}
	}

	// A timeline of no frames, which check() reports, is counted as one rather than divided by.
	const TimelineScale none = scaleOf(0);
	CHECK(none.slot() == 800.0);
	CHECK(none.frameAt(none.xOf(0)) == 0);
}

TEST_CASE("a key past the timeline is drawn in the strip, however far past", "[rigplan][timeline]")
{
	const TimelineScale scale = scaleOf(24, 28.0);
	CHECK_FALSE(scale.beyond(23));
	CHECK(scale.beyond(24));
	CHECK(scale.xOf(24) == scale.xOf(1000000));
	CHECK(scale.xOf(24) > scale.xOf(23));
	CHECK(scale.xOf(24) < scale.left + scale.width);
	CHECK(scale.xOf(23) < scale.left + scale.width - scale.overflow);

	// Which plans have one.
	rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"}); // board keys at 0 and 10, 12 frames
	CHECK_FALSE(rigplan::keysBeyond(plan));
	plan.timeline.frames = 10;
	CHECK(rigplan::keysBeyond(plan));
	plan.timeline.frames = 11;
	CHECK_FALSE(rigplan::keysBeyond(plan));
	plan.props.front().path = rp::Path{{rp::PathKey{11, {}, {}}}};
	CHECK(rigplan::keysBeyond(plan));
}

TEST_CASE("a click picks the nearest tick within its radius, the earlier on a tie", "[rigplan][timeline]")
{
	const TimelineScale scale = scaleOf(24, 28.0);
	const double s = scale.slot();
	const double radius = s * 0.5;
	const rp::Path path{{rp::PathKey{0, {}, {}}, rp::PathKey{5, {}, {}}, rp::PathKey{6, {}, {}}, rp::PathKey{30, {}, {}}, rp::PathKey{40, {}, {}}}};

	CHECK(rigplan::nearestKey(scale, path, scale.xOf(0), radius) == std::optional<std::size_t>(0));
	CHECK(rigplan::nearestKey(scale, path, scale.xOf(5) + 0.4 * s, radius) == std::optional<std::size_t>(1));
	CHECK(rigplan::nearestKey(scale, path, scale.xOf(6) - 0.4 * s, radius) == std::optional<std::size_t>(2));
	// Half way between two ticks is a tie, which the earlier key wins: on slots of 10 points, so the tie is
	// exact rather than left to rounding.
	TimelineScale exact;
	exact.width = 240.0;
	exact.frames = 24;
	REQUIRE(exact.xOf(5) == 55.0);
	CHECK(rigplan::nearestKey(exact, path, 60.0, 5.0) == std::optional<std::size_t>(1));
	// Further than the radius from every tick picks none.
	CHECK_FALSE(rigplan::nearestKey(scale, path, scale.xOf(12), radius));
	// Every key past the timeline is in the strip, and the strip picks the first of them.
	CHECK(rigplan::nearestKey(scale, path, scale.xOf(24), radius) == std::optional<std::size_t>(3));
	CHECK_FALSE(rigplan::nearestKey(scale, rp::Path{}, scale.xOf(0), radius));
}
