// media::capturedBefore: capture order over frame references, the order a method or a report means
// when it speaks of one.

#include <lain/media/frameref.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace lain;
using namespace lain::media;

static FrameRef frame(const char* source, std::size_t ordinal, double milliseconds)
{
	return FrameRef{core::Uri{source}, ordinal, core::Time::from<core::Time::Milliseconds>(milliseconds)};
}

TEST_CASE("capture order is by timestamp, then ordinal, then source", "[media][frameref]")
{
	// The timestamp decides first, whatever the ordinal or source say.
	CHECK(capturedBefore(frame("/z", 9, 40), frame("/a", 0, 80)));
	CHECK_FALSE(capturedBefore(frame("/a", 0, 80), frame("/z", 9, 40)));
	// Then the ordinal, at one timestamp (a source that declares no timing reports zero for all).
	CHECK(capturedBefore(frame("/z", 1, 0), frame("/a", 2, 0)));
	// Then the source, which only frames tied on both reach.
	CHECK(capturedBefore(frame("/a", 3, 0), frame("/b", 3, 0)));
	CHECK_FALSE(capturedBefore(frame("/b", 3, 0), frame("/a", 3, 0)));
}

TEST_CASE("two frames are equivalent in capture order exactly when they are equal", "[media][frameref]")
{
	const FrameRef a = frame("/cam0", 4, 160);
	CHECK_FALSE(capturedBefore(a, a));
	for (const FrameRef& other : {frame("/cam0", 5, 160), frame("/cam1", 4, 160), frame("/cam0", 4, 200)})
	{
		REQUIRE(other != a);
		CHECK(capturedBefore(a, other) != capturedBefore(other, a));
	}

	// So sorting puts a frame given twice beside its twin, where a neighbour check finds it.
	std::vector<FrameRef> frames{frame("/b", 2, 0), frame("/a", 2, 0), frame("/c", 1, 0), frame("/b", 2, 0)};
	std::sort(frames.begin(), frames.end(), capturedBefore);
	CHECK(frames[0] == frame("/c", 1, 0));
	CHECK(frames[1] == frame("/a", 2, 0));
	CHECK(frames[2] == frames[3]);
}
