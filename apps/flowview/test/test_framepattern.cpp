// The output side of a render: a ####-numbered pattern turned into one frame's path.
//
// The range side moved to core::Range (libs/core/test/test_range.cpp) once `--frame` became a typed
// cli option rather than a string this app parsed.

#include "framepattern.h"

#include <catch2/catch_test_macros.hpp>

using namespace flowview;

TEST_CASE("a frame's output path substitutes into the #### field", "[flowview][framepattern]")
{
	CHECK(frameOutputPath("out.####.png", 7) == "out.0007.png");
	CHECK(frameOutputPath("/tmp/shot.##.exr", 3) == "/tmp/shot.03.exr");
	CHECK(frameOutputPath("out.####.png", 0) == "out.0000.png");

	// The width is a padding convention, not a limit — a frame too wide for the field widens it
	// rather than being truncated, and the reader accepts any run of digits, so the result is still
	// found when the sequence is read back.
	CHECK(frameOutputPath("out.##.png", 12345) == "out.12345.png");
}

TEST_CASE("a pattern without a number field cannot name a frame", "[flowview][framepattern]")
{
	CHECK(isFramePattern("out.####.png"));
	CHECK(isFramePattern("#"));
	CHECK_FALSE(isFramePattern("out.png"));

	// Which is why a multi-frame sweep refuses one: writing every frame to the same path exits
	// reporting success while having destroyed the correspondence between input and output frames.
	CHECK(frameOutputPath("out.png", 7) == "out.png");
}
