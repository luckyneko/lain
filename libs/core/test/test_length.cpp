// Unit tests for lain::core::Length: exact int64-nanometre storage behind named units.

#include "lain/core/length.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using lain::core::Length;

TEST_CASE("one quantity is one Length, whatever unit built it", "[length]")
{
	const Length mm = Length::fromMillimetres(24.0);
	REQUIRE(mm == Length::fromMetres(0.024));
	REQUIRE(mm == Length::fromMicrometres(24000.0));
	REQUIRE(mm == Length::fromNanometres(24000000));
	REQUIRE(mm.nanometres() == 24000000);
}

TEST_CASE("sums are exact where a double in metres is not", "[length]")
{
	// In metres, 0.0001 + 0.0002 != 0.0003. In nanometres there is nothing to round.
	REQUIRE(Length::fromMillimetres(0.1) + Length::fromMillimetres(0.2) == Length::fromMillimetres(0.3));
	REQUIRE(Length::fromMillimetres(0.3) - Length::fromMillimetres(0.1) == Length::fromMillimetres(0.2));
}

TEST_CASE("a unit value rounds to the nearest nanometre", "[length]")
{
	REQUIRE(Length::fromMetres(1.4e-9).nanometres() == 1);
	REQUIRE(Length::fromMetres(1.6e-9).nanometres() == 2);
	REQUIRE(Length::fromMetres(-1.6e-9).nanometres() == -2);
	REQUIRE(Length::fromMicrometres(0.0004).nanometres() == 0);
}

TEST_CASE("Length reads in named units and behaves as a quantity", "[length]")
{
	const Length square = Length::fromMillimetres(24.0);
	REQUIRE(square.metres() == Catch::Approx(0.024));
	REQUIRE(square.millimetres() == Catch::Approx(24.0));
	REQUIRE(square.micrometres() == Catch::Approx(24000.0));

	REQUIRE(square * 0.75 == Length::fromMillimetres(18.0));
	REQUIRE(Length::fromMillimetres(48.0) / square == Catch::Approx(2.0));
	REQUIRE(-square < Length{});
	REQUIRE(Length{} < square);
	REQUIRE(square <= square);
	REQUIRE(square >= Length::fromMillimetres(23.999));
	REQUIRE(square != Length::fromMillimetres(23.999));
}

#ifdef NDEBUG
TEST_CASE("an invalid unit value is made defined in release, never undefined", "[length]")
{
	// A precondition, asserted in debug. In release a NaN becomes zero and an out-of-range value
	// saturates, so nothing reaches llround's undefined behaviour.
	REQUIRE(Length::fromMetres(std::numeric_limits<double>::quiet_NaN()) == Length{});
	REQUIRE(Length::fromMetres(std::numeric_limits<double>::infinity()).nanometres() ==
			std::numeric_limits<std::int64_t>::max());
	REQUIRE(Length::fromMetres(-1e30).nanometres() == std::numeric_limits<std::int64_t>::min());
}
#endif
