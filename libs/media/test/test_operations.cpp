#include "testsource.h"

#include <lain/media/framesequence.h>
#include <lain/media/operations.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace lain;
using namespace lain::media;
using namespace lain::media::test;

// A 10-frame sequence over one source, the starting point for most of these.
static FrameSequence tenFrames(const std::shared_ptr<TestSource>& source)
{
	return FrameSequence::over(source);
}

TEST_CASE("clip re-bases position and preserves identity", "[media][operations]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 10);
	const FrameSequence sequence = tenFrames(source);

	const FrameSequence clipped = clip(sequence, 4, 3);
	REQUIRE(clipped.size() == 3);

	// The distinction the whole model rests on: position 0 of the clip is still ordinal 4 of its
	// source, and the pixels come from there too.
	CHECK(clipped.frame(0).ordinal == 4);
	CHECK(TestSource::tagOf(clipped.image(0)) == 4);
	CHECK(clipped.frame(2).ordinal == 6);

	// The spec survives a clip untouched — nothing was added, so nothing can disagree.
	CHECK(clipped.spec() == sequence.spec());
}

TEST_CASE("clip clamps rather than refusing", "[media][operations]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 10);
	const FrameSequence sequence = tenFrames(source);

	// Asking for more than exists is an ordinary thing to do at the end of a timeline; the
	// honest answer is what there was.
	CHECK(clip(sequence, 8, 50).size() == 2);
	CHECK(clip(sequence, 0, 0).empty());

	// A start past the end has nothing to clamp to.
	CHECK(clip(sequence, 10, 5).empty());
	CHECK(clip(sequence, 99, 5).empty());
}

TEST_CASE("concat appends, and enforces homogeneity at the join", "[media][operations]")
{
	auto first = std::make_shared<TestSource>("/footage/take1", 3);
	auto second = std::make_shared<TestSource>("/footage/take2", 2);

	const std::optional<FrameSequence> joined = concat(FrameSequence::over(first), FrameSequence::over(second));
	REQUIRE(joined.has_value());
	REQUIRE(joined->size() == 5);
	CHECK(joined->frame(0).source == "/footage/take1");
	CHECK(joined->frame(3).source == "/footage/take2");
	CHECK(joined->frame(3).ordinal == 0); // identity re-based? no — preserved

	SECTION("with an empty sequence, either way round")
	{
		// An empty sequence contributes no entries and therefore no constraint. Without that,
		// building a timeline up from nothing would refuse on its first step.
		REQUIRE(concat(FrameSequence{}, FrameSequence::over(first)).has_value());
		CHECK(concat(FrameSequence{}, FrameSequence::over(first))->size() == 3);
		CHECK(concat(FrameSequence::over(first), FrameSequence{})->size() == 3);
		CHECK(concat(FrameSequence{}, FrameSequence{})->empty());
	}

	SECTION("a mismatched spec is refused")
	{
		FrameSpec other = testSpec();
		other.pixelFormat = image::PixelFormat::RGB8;
		auto mismatched = std::make_shared<TestSource>("/footage/take3", 2, other);

		// Refused at the point of composition, so a heterogeneous sequence cannot be built at
		// all rather than being discovered halfway through a render.
		CHECK_FALSE(concat(FrameSequence::over(first), FrameSequence::over(mismatched)).has_value());
	}
}

TEST_CASE("reverse reverses", "[media][operations]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 4);
	const FrameSequence reversed = reverse(FrameSequence::over(source));

	REQUIRE(reversed.size() == 4);
	CHECK(reversed.frame(0).ordinal == 3);
	CHECK(reversed.frame(3).ordinal == 0);
	CHECK(TestSource::tagOf(reversed.image(0)) == 3);

	CHECK(reverse(FrameSequence{}).empty());
}

TEST_CASE("stride takes every nth frame", "[media][operations]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 10);
	const FrameSequence sequence = tenFrames(source);

	const FrameSequence every3rd = stride(sequence, 3);
	REQUIRE(every3rd.size() == 4); // 0, 3, 6, 9
	CHECK(every3rd.frame(0).ordinal == 0);
	CHECK(every3rd.frame(3).ordinal == 9);

	CHECK(stride(sequence, 1).size() == 10); // the identity
	CHECK(stride(sequence, 99).size() == 1); // a step past the end still yields frame 0

	// A step of 0 is a caller error, and reading it as 1 would return a sequence that looks
	// right and means something else — so it is empty and loud instead.
	CHECK(stride(sequence, 0).empty());
}

TEST_CASE("select is an arbitrary subset, in the order asked", "[media][operations]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 10);
	const FrameSequence sequence = tenFrames(source);

	// This is calibration view selection: the frames a metric chose, in whatever order, with
	// repeats allowed.
	const std::optional<FrameSequence> chosen = select(sequence, {7, 1, 7, 4});
	REQUIRE(chosen.has_value());
	REQUIRE(chosen->size() == 4);
	CHECK(chosen->frame(0).ordinal == 7);
	CHECK(chosen->frame(1).ordinal == 1);
	CHECK(chosen->frame(2).ordinal == 7);
	CHECK(TestSource::tagOf(chosen->image(3)) == 4);

	CHECK(select(sequence, {})->empty());

	// One bad position refuses the whole selection rather than silently returning a shorter one:
	// dropping it would corrupt the correspondence between what was chosen and what came back,
	// the same reasoning that makes a map with one suppressed element clear its whole output.
	CHECK_FALSE(select(sequence, {1, 10, 2}).has_value());
}

TEST_CASE("operations compose, and identity survives all of them", "[media][operations]")
{
	auto first = std::make_shared<TestSource>("/footage/take1", 6);
	auto second = std::make_shared<TestSource>("/footage/take2", 6);

	// clip-of-concat-of-clip — the shape ADR-0018 rejected a wrapper hierarchy for, because
	// nobody would own flattening the chain. As a list operation it is just another vector.
	const std::optional<FrameSequence> joined = concat(clip(FrameSequence::over(first), 4, 2),
													   clip(FrameSequence::over(second), 0, 2));
	REQUIRE(joined.has_value());

	const FrameSequence result = reverse(*joined);
	REQUIRE(result.size() == 4);

	// take2/1, take2/0, take1/5, take1/4 — every frame still names the source and ordinal it
	// started as, after three operations.
	CHECK(result.frame(0).source == "/footage/take2");
	CHECK(result.frame(0).ordinal == 1);
	CHECK(result.frame(3).source == "/footage/take1");
	CHECK(result.frame(3).ordinal == 4);
	CHECK(TestSource::tagOf(result.image(3)) == 4);
}
