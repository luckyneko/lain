// The transport clock (M15 slice 9a), which flowview's sequence player and rigplan's Timeline share.
// Every time here is a sum of powers of two, so it is exact, and a frame boundary is hit on the dot.

#include <lain/editor/playback.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>

using lain::editor::Playback;

TEST_CASE("playback moves by whole frames of the time that has passed, never ahead of it", "[editor][playback]")
{
	Playback playback;
	playback.play();
	// 8 frames a second: a frame every 0.125 s, here in steps of 0.0625 s.
	std::size_t position = 0;
	position = playback.advance(position, 99, 8.0, 0.0625);
	CHECK(position == 0); // half a frame's time: not yet
	position = playback.advance(position, 99, 8.0, 0.0625);
	CHECK(position == 1);
	position = playback.advance(position, 99, 8.0, 0.0625);
	CHECK(position == 1); // a frame and a half: still the first, never rounded up to the second
	position = playback.advance(position, 99, 8.0, 0.0625);
	CHECK(position == 2);

	SECTION("a frame that took too long skips the frames it had no time for")
	{
		CHECK(playback.advance(position, 99, 8.0, 0.5) == 6);
	}
	SECTION("what is left over is carried, not dropped")
	{
		position = playback.advance(position, 99, 8.0, 0.1875); // a frame and a half
		CHECK(position == 3);
		CHECK(playback.advance(position, 99, 8.0, 0.0625) == 4); // the half carried, and a half more
	}
}

TEST_CASE("without a loop playback stops at the last frame; with one it goes round", "[editor][playback]")
{
	SECTION("stops")
	{
		Playback playback;
		playback.play();
		CHECK(playback.advance(8, 9, 8.0, 0.125) == 9);
		CHECK_FALSE(playback.playing());
		CHECK(playback.advance(9, 9, 8.0, 1.0) == 9); // paused there
	}
	SECTION("a stretch that would pass the end also stops at it")
	{
		Playback playback;
		playback.play();
		CHECK(playback.advance(7, 9, 8.0, 1.0) == 9);
		CHECK_FALSE(playback.playing());
	}
	SECTION("goes round")
	{
		Playback playback(true);
		REQUIRE(playback.loops());
		playback.play();
		CHECK(playback.advance(9, 9, 8.0, 0.125) == 0);
		CHECK(playback.playing());
		CHECK(playback.advance(8, 9, 8.0, 0.375) == 1); // 9, 0, 1
		CHECK(playback.advance(0, 9, 8.0, 1.25) == 0);	// ten frames: once round
	}
	SECTION("a one-frame timeline stays on it")
	{
		Playback playback(true);
		playback.play();
		CHECK(playback.advance(0, 0, 8.0, 1.0) == 0);
		CHECK(playback.playing());
	}
}

TEST_CASE("paused, or at no usable rate, playback stays put", "[editor][playback]")
{
	Playback playback;
	CHECK(playback.advance(3, 9, 8.0, 1.0) == 3);

	playback.play();
	CHECK(playback.advance(3, 9, 0.0, 1.0) == 3);
	CHECK(playback.advance(3, 9, -8.0, 1.0) == 3);
	CHECK(playback.advance(3, 9, std::numeric_limits<double>::quiet_NaN(), 1.0) == 3);
	CHECK(playback.advance(3, 9, 8.0, -1.0) == 3);
	CHECK(playback.playing());

	SECTION("a position past the end is held at it")
	{
		Playback paused;
		CHECK(paused.advance(40, 9, 8.0, 0.0) == 9);
	}
	SECTION("starting again owes nothing for the time spent paused")
	{
		CHECK(playback.advance(3, 9, 8.0, 0.0625) == 3); // half a frame toward the next
		playback.pause();
		playback.play();
		CHECK(playback.advance(3, 9, 8.0, 0.0625) == 3); // the half before the pause is gone
	}
	SECTION("a stall of any length lands on a frame of the timeline")
	{
		Playback looping(true);
		looping.play();
		CHECK(looping.advance(3, 9, 8.0, 1e300) <= 9);
		Playback stopping;
		stopping.play();
		CHECK(stopping.advance(3, 9, 8.0, 1e300) == 9);
	}
}
