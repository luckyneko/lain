#pragma once

// A board for the tests: the common 7x5 ChArUco on ARUCO_5X5_100, printed with 24 mm squares unless a
// test needs it seen from further away.

#include <lain/camera/board/specification.h>

#include <catch2/catch_test_macros.hpp>

namespace lain::camera::testing
{
	inline board::PatternParameters patternParameters()
	{
		board::PatternParameters p;
		p.dictionary = board::Dictionary::Aruco5x5_100;
		p.squaresX = 7;
		p.squaresY = 5;
		p.markerToSquare = 0.75;
		return p;
	}

	inline board::Pattern pattern(const board::PatternParameters& p = patternParameters())
	{
		board::PatternResult result = board::Pattern::create(p);
		REQUIRE(result.pattern.has_value());
		return *result.pattern;
	}

	inline board::Specification specification(const board::Pattern& p = pattern(), double millimetres = 24.0)
	{
		board::Instance instance;
		instance.identity = "test board";
		instance.squareLength.value = core::Length::from<core::Length::Millimetres>(millimetres);
		board::SpecificationResult result = board::Specification::create(p, instance);
		REQUIRE(result.specification.has_value());
		return *result.specification;
	}
} // namespace lain::camera::testing
