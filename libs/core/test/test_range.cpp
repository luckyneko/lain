// core::Range — the inclusive arithmetic range and its literal form.
//
// The parse cases came from flowview's frame sweep, which is why they read as frame ranges; they
// are the reason the type exists, not the limit of it.

#include "lain/core/range.h"

#include <catch2/catch_test_macros.hpp>

using lain::core::Range;

TEST_CASE("a bare number is a one-value range", "[core][range]")
{
	const auto range = Range::parse("12");
	REQUIRE(range.has_value());
	REQUIRE(range->first == 12);
	REQUIRE(range->last == 12);
	REQUIRE(range->step == 1);
	REQUIRE(range->count() == 1);
}

TEST_CASE("a range is inclusive at both ends", "[core][range]")
{
	const auto range = Range::parse("0-499");
	REQUIRE(range.has_value());
	REQUIRE(range->first == 0);
	REQUIRE(range->last == 499);

	// Inclusive: "0-499" reads as 500 things and must mean 500 things.
	REQUIRE(range->count() == 500);
	REQUIRE(Range::parse("7-7")->count() == 1);
}

TEST_CASE("a step takes every nth value", "[core][range]")
{
	const auto range = Range::parse("0-9x3");
	REQUIRE(range.has_value());
	REQUIRE(range->step == 3);
	REQUIRE(range->count() == 4); // 0, 3, 6, 9

	// The end is visited only when the step lands on it — "every third from 0", not "four values
	// spread across the range".
	REQUIRE(Range::parse("0-10x3")->count() == 4);
}

TEST_CASE("contains reports the values visited, not merely the bounds", "[core][range]")
{
	const Range range{0, 9, 3};
	REQUIRE(range.contains(0));
	REQUIRE(range.contains(9));
	REQUIRE_FALSE(range.contains(4)); // inside the bounds, not on the step
	REQUIRE_FALSE(range.contains(12));
}

TEST_CASE("a range that could not mean what it says is refused", "[core][range]")
{
	REQUIRE_FALSE(Range::parse("").has_value());
	REQUIRE_FALSE(Range::parse("abc").has_value());
	REQUIRE_FALSE(Range::parse("0-").has_value());
	REQUIRE_FALSE(Range::parse("-5").has_value());	  // no negatives, and not a range either
	REQUIRE_FALSE(Range::parse("10-2").has_value());  // reversed: reversing is a collection's job
	REQUIRE_FALSE(Range::parse("0-9x0").has_value()); // a zero step would never terminate
	REQUIRE_FALSE(Range::parse("0-9x").has_value());
	REQUIRE_FALSE(Range::parse("0-9 ").has_value()); // trailing junk
	REQUIRE_FALSE(Range::parse("0-9frames").has_value());

	// Refused rather than wrapped: a wrapped range visits the wrong values and says nothing.
	REQUIRE_FALSE(Range::parse("99999999999999999999999999").has_value());
}

TEST_CASE("toString and parse round-trip", "[core][range]")
{
	// The literal form is the type's own, as core::Version's is — so what a command line accepts is
	// exactly what the value prints.
	for (const char* text : {"12", "0-499", "0-499x2", "7-7"})
	{
		const auto parsed = Range::parse(text);
		REQUIRE(parsed.has_value());
		REQUIRE(Range::parse(parsed->toString()) == parsed);
	}

	// The short forms are canonical: a degenerate range prints as one number, a step of 1 is
	// implied, so the printed form is the one a human would have typed.
	REQUIRE(Range{7, 7, 1}.toString() == "7");
	REQUIRE(Range{0, 9, 1}.toString() == "0-9");
	REQUIRE(Range{0, 9, 2}.toString() == "0-9x2");
}

TEST_CASE("lexical_cast is the hook a command line converts through", "[core][range]")
{
	// CLI11 finds this by ADL on Range; core names nothing of CLI11 to provide it.
	Range range;
	REQUIRE(lexical_cast(std::string{"0-9x3"}, range));
	REQUIRE(range == Range{0, 9, 3});

	Range untouched{1, 2, 1};
	REQUIRE_FALSE(lexical_cast(std::string{"nonsense"}, untouched));
	REQUIRE(untouched == Range{1, 2, 1}); // a refused parse leaves the target alone
}
