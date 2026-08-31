#include "testsource.h"

#include <lain/media/framespec.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace lain;
using namespace lain::media;
using namespace lain::media::test;

TEST_CASE("a frame rate is exact, and absence is a state", "[media][spec]")
{
	CHECK_FALSE(FrameRate{}.specified());
	CHECK(FrameRate{}.hz() == 0.0);
	CHECK(FrameRate{}.toString() == "unspecified");

	// The reason the rate is rational rather than a double: 29.97 is not a rate, 30000/1001 is.
	const FrameRate ntsc{30000, 1001};
	CHECK(ntsc.specified());
	CHECK_THAT(ntsc.hz(), Catch::Matchers::WithinRel(29.97, 1e-4));

	// A whole rate reads whole rather than "25.00 fps".
	CHECK(FrameRate{25, 1}.toString() == "25 fps");

	// A zero denominator is as unspecified as a zero numerator — it names no rate either.
	CHECK_FALSE((FrameRate{25, 0}.specified()));
}

TEST_CASE("a spec describes an image, and matching ignores the rate", "[media][spec]")
{
	image::Image image{4, 2, image::PixelFormat::RGBA8, image::ColorSpace::sRGB, image::AlphaMode::Straight};

	const FrameSpec spec = specOf(image, FrameRate{25, 1});
	CHECK(spec.valid());
	CHECK(spec.extent == image.extent());
	CHECK(spec.rate == FrameRate{25, 1});

	// The rate is a property of the sequence, not of any one image, so an image can match a spec
	// carrying one — it has no rate to disagree with.
	CHECK(matches(spec, image));

	image::Image wrongSize{8, 2, image::PixelFormat::RGBA8, image::ColorSpace::sRGB, image::AlphaMode::Straight};
	CHECK_FALSE(matches(spec, wrongSize));

	image::Image wrongSpace{4, 2, image::PixelFormat::RGBA8, image::ColorSpace::Linear, image::AlphaMode::Straight};
	CHECK_FALSE(matches(spec, wrongSpace));

	CHECK_FALSE(matches(spec, image::Image{}));
}

TEST_CASE("unify enforces homogeneity, and an unspecified rate adopts", "[media][spec]")
{
	const FrameSpec twentyFive = testSpec(FrameRate{25, 1});
	const FrameSpec rateless = testSpec(FrameRate{});

	SECTION("identical specs unify to themselves")
	{
		REQUIRE(unify(twentyFive, twentyFive).has_value());
		CHECK(*unify(twentyFive, twentyFive) == twentyFive);
	}

	SECTION("an invalid spec imposes no constraint")
	{
		// This is what lets concat() append to an EMPTY sequence, whose spec is invalid — without
		// it, building anything up from empty would refuse on the first step.
		REQUIRE(unify(FrameSpec{}, twentyFive).has_value());
		CHECK(*unify(FrameSpec{}, twentyFive) == twentyFive);
		CHECK(*unify(twentyFive, FrameSpec{}) == twentyFive);
	}

	SECTION("pixel geometry and colour tags must agree exactly")
	{
		FrameSpec bigger = twentyFive;
		bigger.extent = {8, 2};
		CHECK_FALSE(unify(twentyFive, bigger).has_value());

		FrameSpec linear = twentyFive;
		linear.colorSpace = image::ColorSpace::Linear;
		CHECK_FALSE(unify(twentyFive, linear).has_value());

		FrameSpec premultiplied = twentyFive;
		premultiplied.alphaMode = image::AlphaMode::Premultiplied;
		CHECK_FALSE(unify(twentyFive, premultiplied).has_value());
	}

	SECTION("an unspecified rate adopts the other side's")
	{
		// ADR-0018 promises a sequence can span "a video file and a folder of stills". The stills
		// declare no rate, so this is the case that makes that promise true rather than a
		// refusal — and it must work in both orders.
		REQUIRE(unify(rateless, twentyFive).has_value());
		CHECK(unify(rateless, twentyFive)->rate == FrameRate{25, 1});
		REQUIRE(unify(twentyFive, rateless).has_value());
		CHECK(unify(twentyFive, rateless)->rate == FrameRate{25, 1});
	}

	SECTION("two different declared rates are refused")
	{
		// Reconciling them is a retime, which no composition should decide silently.
		CHECK_FALSE(unify(twentyFive, testSpec(FrameRate{30, 1})).has_value());
	}
}
