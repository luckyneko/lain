// The output side of a render: a pattern naming io::image::frameKey turned into one frame's path.
//
// The grammar itself is lain::string's and is tested there; what is tested here is the app's half
// of the agreement — that the sweep names the key the opener matches, and that a pattern which
// cannot name a frame is recognised as such.
//
// The range side moved to core::Range (libs/core/test/test_range.cpp) once `--frame` became a typed
// cli option rather than a string this app parsed.

#include "framepattern.h"

#include <catch2/catch_test_macros.hpp>

using namespace flowview;

TEST_CASE("a frame's output path substitutes into the frame field", "[flowview][framepattern]")
{
	CHECK(*frameOutputPath("out.<frame:04>.png", 7) == "out.0007.png");
	CHECK(*frameOutputPath("/tmp/shot.<frame:02>.exr", 3) == "/tmp/shot.03.exr");
	CHECK(*frameOutputPath("out.<frame:04>.png", 0) == "out.0000.png");

	// The width is a padding convention, not a limit — a frame too wide for the field widens it
	// rather than being truncated, and the reader matches on the key rather than a digit count, so
	// the result is still found when the sequence is read back.
	CHECK(*frameOutputPath("out.<frame:02>.png", 12345) == "out.12345.png");

	// An unpadded key is legal and means exactly what it says.
	CHECK(*frameOutputPath("out.<frame>.png", 7) == "out.7.png");
}

TEST_CASE("a pattern without a frame field cannot name a frame", "[flowview][framepattern]")
{
	CHECK(isFramePattern("out.<frame:04>.png"));
	CHECK(isFramePattern("<frame>"));
	CHECK_FALSE(isFramePattern("out.png"));

	// The retired spelling is not a frame pattern, which is what makes a sweep refuse it with a
	// message rather than write every frame to one file.
	CHECK_FALSE(isFramePattern("out.####.png"));

	// A key that is not the frame key names something this app cannot fill.
	CHECK_FALSE(isFramePattern("out.<take:04>.png"));

	// Which is why a multi-frame sweep refuses one: writing every frame to the same path exits
	// reporting success while having destroyed the correspondence between input and output frames.
	CHECK(*frameOutputPath("out.png", 7) == "out.png");
}

TEST_CASE("a spec that cannot be filled is a refusal, not a path", "[flowview][framepattern]")
{
	// A pattern is text the caller typed, so the sweep has to be able to report it rather than
	// writing one filename per frame or throwing out of the render.
	CHECK_FALSE(frameOutputPath("out.<frame:zz>.png", 7).has_value());
}
