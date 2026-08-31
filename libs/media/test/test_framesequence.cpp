#include "testsource.h"

#include <lain/media/framesequence.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace lain;
using namespace lain::media;
using namespace lain::media::test;

TEST_CASE("an empty sequence is a value, not a failure", "[media][sequence]")
{
	const FrameSequence empty;
	CHECK(empty.empty());
	CHECK(empty.size() == 0);
	CHECK_FALSE(empty.spec().valid()); // the honest answer to "what shape are no frames"
	CHECK(empty.toString() == "0 frames");

	// Failure is std::nullopt, as it is for io::image::load. Zero frames of nothing is what an
	// empty folder legitimately produces, and the two must stay distinguishable — the same
	// distinction ADR-0014 draws for a map over an empty collection.
	CHECK_FALSE(empty.frame(0).valid());
	CHECK_FALSE(empty.image(0).valid());
}

TEST_CASE("a sequence over one source lists every frame", "[media][sequence]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 5);
	const FrameSequence sequence = FrameSequence::over(source);

	CHECK(sequence.size() == 5);
	CHECK(sequence.spec() == source->spec());
	CHECK(sequence.frame(2).ordinal == 2);
	CHECK(TestSource::tagOf(sequence.image(2)) == 2);
	CHECK(sequence.toString() == "5 frames · 4x2 RGBA8 sRGB · 25 fps");

	CHECK(FrameSequence::over(nullptr).empty());
}

TEST_CASE("a sequence spans several sources when their specs unify", "[media][sequence]")
{
	// The multi-file timeline ADR-0018 says comes free from the list form — CONTEXT.md's
	// camera-sequence definition over media segments, with no new concept.
	auto first = std::make_shared<TestSource>("/footage/take1", 3);
	auto second = std::make_shared<TestSource>("/footage/take2", 2);

	std::vector<FrameSequence::Entry> entries;
	entries.push_back({first, 2});
	entries.push_back({second, 0});
	entries.push_back({first, 0});

	const std::optional<FrameSequence> sequence = FrameSequence::of(std::move(entries));
	REQUIRE(sequence.has_value());
	REQUIRE(sequence->size() == 3);

	// Position is where a frame sits; identity is which frame of which source it IS. Here they
	// deliberately disagree at every position.
	CHECK(sequence->frame(0).source == "/footage/take1");
	CHECK(sequence->frame(0).ordinal == 2);
	CHECK(sequence->frame(1).source == "/footage/take2");
	CHECK(sequence->frame(1).ordinal == 0);
	CHECK(sequence->frame(2).ordinal == 0);

	// And the pixels follow identity, not position.
	CHECK(TestSource::tagOf(sequence->image(0)) == 2);
	CHECK(TestSource::tagOf(sequence->image(2)) == 0);
}

TEST_CASE("a stills source joins a rated one, adopting its rate", "[media][sequence]")
{
	auto video = std::make_shared<TestSource>("/footage/take1.mp4", 2, testSpec(FrameRate{25, 1}));
	auto stills = std::make_shared<TestSource>("/footage/stills", 2, testSpec(FrameRate{}));

	const std::optional<FrameSequence> sequence =
		FrameSequence::of({{video, 0}, {stills, 0}, {stills, 1}, {video, 1}});
	REQUIRE(sequence.has_value());
	CHECK(sequence->spec().rate == FrameRate{25, 1});
}

TEST_CASE("a sequence refuses entries it could not deliver", "[media][sequence]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 3);

	SECTION("an ordinal past the end of its source")
	{
		// Refused at composition rather than discovered mid-render, which is the whole reason
		// homogeneity and range are checked here.
		CHECK_FALSE(FrameSequence::of({{source, 3}}).has_value());
	}

	SECTION("an entry naming no source")
	{
		CHECK_FALSE(FrameSequence::of({{nullptr, 0}}).has_value());
	}

	SECTION("a source whose spec cannot unify")
	{
		FrameSpec other = testSpec();
		other.extent = {8, 2};
		auto mismatched = std::make_shared<TestSource>("/footage/take2", 2, other);

		CHECK_FALSE(FrameSequence::of({{source, 0}, {mismatched, 0}}).has_value());
	}
}

TEST_CASE("copying a sequence copies no decoder", "[media][sequence]")
{
	auto source = std::make_shared<TestSource>("/footage/take1", 4);
	const FrameSequence sequence = FrameSequence::over(source);

	REQUIRE(sequence.image(1).valid());
	CHECK(source->decodeCount == 1);

	// A copy shares its sources by shared_ptr, so it shares their caches too — which is what
	// makes a sequence cheap enough to ride a flow port like any other payload.
	const FrameSequence copy = sequence;
	CHECK(copy.size() == 4);
	CHECK(TestSource::tagOf(copy.image(1)) == 1);
	CHECK(source->decodeCount == 1);
}
