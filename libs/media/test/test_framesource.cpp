#include "testsource.h"

#include <lain/media/framesource.h>

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace lain;
using namespace lain::media;
using namespace lain::media::test;

TEST_CASE("a source reports identity without decoding", "[media][source]")
{
	TestSource source{"/footage/take1", 10};

	const FrameRef ref = source.frame(3);
	CHECK(ref.valid());
	CHECK(ref.source == "/footage/take1");
	CHECK(ref.ordinal == 3);

	// The whole point of separating identity from pixels: a timeline, a manifest and an issue
	// all name frames, and none of them should pay a decode to do it.
	CHECK(source.decodeCount == 0);

	// Derived from the nominal rate by the base class — 3 frames at 25 fps.
	CHECK(ref.timestamp.seconds() == 0.12);

	CHECK_FALSE(source.frame(10).valid());
}

TEST_CASE("a source decodes lazily and memoises", "[media][source]")
{
	TestSource source{"/footage/take1", 10};

	const image::Image first = source.image(4);
	REQUIRE(first.valid());
	CHECK(TestSource::tagOf(first) == 4);
	CHECK(source.decodeCount == 1);

	// The second read is served from the ring. Counting decodes is what makes this an assertion
	// about the cache rather than about equality — both paths return an equal image.
	const image::Image again = source.image(4);
	CHECK(TestSource::tagOf(again) == 4);
	CHECK(source.decodeCount == 1);
}

TEST_CASE("an evicted frame stays valid in the hands of its caller", "[media][source]")
{
	// A ring of one, so the very next read evicts.
	TestSource source{"/footage/take1", 10, testSpec(), 1};

	const image::Image held = source.image(0);
	REQUIRE(held.valid());

	for (std::size_t ordinal = 1; ordinal < 5; ++ordinal)
		CHECK(source.image(ordinal).valid());

	// "Decoded images own their pixels and stay valid after cache eviction" (CONTEXT.md). The
	// Image the caller holds is its own copy, so eviction cannot reach it — the property that
	// lets a decoded frame ride a flow port for as long as an evaluation retains it.
	CHECK(held.valid());
	CHECK(TestSource::tagOf(held) == 0);

	// And frame 0 really was evicted, so it costs a decode again.
	const int before = source.decodeCount;
	CHECK(source.image(0).valid());
	CHECK(source.decodeCount == before + 1);
}

TEST_CASE("a source with no cache still works", "[media][source]")
{
	TestSource source{"/footage/take1", 4, testSpec(), 0};

	CHECK(TestSource::tagOf(source.image(1)) == 1);
	CHECK(TestSource::tagOf(source.image(1)) == 1);
	CHECK(source.decodeCount == 2); // every read decodes, and none of them crashes on an empty ring
}

TEST_CASE("a source refuses what it cannot deliver", "[media][source]")
{
	SECTION("out of range")
	{
		TestSource source{"/footage/take1", 4};
		CHECK_FALSE(source.image(4).valid());
		CHECK(source.decodeCount == 0); // refused before the decoder is asked
	}

	SECTION("a decode failure is not remembered")
	{
		TestSource source{"/footage/take1", 4};
		source.failAt = 2;

		CHECK_FALSE(source.image(2).valid());
		CHECK(source.decodeCount == 1);

		// Caching a failure would make one transient error permanent for the life of the source,
		// so a second attempt really does try again.
		CHECK_FALSE(source.image(2).valid());
		CHECK(source.decodeCount == 2);
	}

	SECTION("a frame that does not match the declared spec")
	{
		TestSource source{"/footage/take1", 4};
		source.mismatchAt = 1;

		// The homogeneity guarantee is enforced on delivery, not merely declared: a consumer
		// receiving this could only cope by converting, on a frame it did not know would differ.
		CHECK_FALSE(source.image(1).valid());
		CHECK(source.image(0).valid()); // its neighbours are unaffected
	}
}

TEST_CASE("concurrent reads of one source are safe", "[media][source]")
{
	// CONTEXT.md promises callers safety, NOT parallelism: "concurrent decode requests are safe
	// even when an implementation serializes access internally". This pins the promise that was
	// made — that eight threads hammering one source produce correct frames and no race — while
	// deliberately asserting nothing about how many decode at once, so the later swap to a
	// decoder pool does not have to break a test to happen.
	TestSource source{"/footage/take1", 16, testSpec(), 2};

	std::vector<std::thread> threads;
	for (int t = 0; t < 8; ++t)
	{
		threads.emplace_back(
			[&source]
			{
				for (std::size_t ordinal = 0; ordinal < 16; ++ordinal)
				{
					const image::Image image = source.image(ordinal);
					REQUIRE(image.valid());
					REQUIRE(TestSource::tagOf(image) == static_cast<std::uint8_t>(ordinal));
				}
			});
	}
	for (std::thread& thread : threads)
		thread.join();
}
