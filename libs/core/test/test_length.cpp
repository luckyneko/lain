// Unit tests for lain::core::Length: exact int64-nanometre storage, read and written in units.

#include "lain/core/length.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using lain::core::Length;

TEST_CASE("one quantity is one Length, whatever unit built it", "[length]")
{
	const Length mm = Length::from<Length::Millimetres>(24.0);
	REQUIRE(mm == Length::from<Length::Metres>(0.024));
	REQUIRE(mm == Length::from<Length::Micrometres>(24000.0));
	REQUIRE(mm == Length::from<Length::Nanometres>(24000000.0));
	REQUIRE(mm.nanometres() == 24000000);
}

TEST_CASE("sums are exact where a double in metres is not", "[length]")
{
	// In metres, 0.0001 + 0.0002 != 0.0003. In nanometres there is nothing to round.
	REQUIRE(Length::from<Length::Millimetres>(0.1) + Length::from<Length::Millimetres>(0.2) ==
			Length::from<Length::Millimetres>(0.3));
	REQUIRE(Length::from<Length::Millimetres>(0.3) - Length::from<Length::Millimetres>(0.1) ==
			Length::from<Length::Millimetres>(0.2));
}

TEST_CASE("a unit value rounds to the nearest nanometre", "[length]")
{
	REQUIRE(Length::from<Length::Metres>(1.4e-9).nanometres() == 1);
	REQUIRE(Length::from<Length::Metres>(1.6e-9).nanometres() == 2);
	REQUIRE(Length::from<Length::Metres>(-1.6e-9).nanometres() == -2);
	REQUIRE(Length::from<Length::Micrometres>(0.0004).nanometres() == 0);
}

TEST_CASE("Length reads in units and behaves as a quantity", "[length]")
{
	const Length square = Length::from<Length::Millimetres>(24.0);
	REQUIRE(square.metres() == Catch::Approx(0.024));
	REQUIRE(square.as<Length::Metres>() == square.metres());
	REQUIRE(square.as<Length::Millimetres>() == Catch::Approx(24.0));
	REQUIRE(square.as<Length::Micrometres>() == Catch::Approx(24000.0));
	REQUIRE(square.as<Length::Nanometres>() == 24000000.0);

	REQUIRE(square * 0.75 == Length::from<Length::Millimetres>(18.0));
	REQUIRE(Length::from<Length::Millimetres>(48.0) / square == Catch::Approx(2.0));
	REQUIRE(-square < Length{});
	REQUIRE(Length{} < square);
	REQUIRE(square <= square);
	REQUIRE(square >= Length::from<Length::Millimetres>(23.999));
	REQUIRE(square != Length::from<Length::Millimetres>(23.999));
}

TEST_CASE("a decimal reads back as exactly the double it was typed as", "[length]")
{
	// What a document writes is as<Millimetres>(), so this is what keeps 0.1 mm "0.1" on disk.
	// Multiplying by a reciprocal a double cannot hold (1e-6, 1e-9) misses 29% of the millimetre
	// values here and 40% of the metre values, 0.1 mm reading back as 0.099999999999999992.
	// Dividing is correctly rounded and misses none.
	for (int micrometres = 0; micrometres <= 100000; ++micrometres)
	{
		const double millimetres = micrometres / 1000.0;
		REQUIRE(Length::from<Length::Millimetres>(millimetres).as<Length::Millimetres>() == millimetres);
	}
	for (int micrometres = 0; micrometres <= 100000; ++micrometres)
	{
		const double metres = micrometres / 1e6;
		REQUIRE(Length::from<Length::Metres>(metres).metres() == metres);
	}
}

#ifdef NDEBUG
TEST_CASE("an invalid unit value is made defined in release, never undefined", "[length]")
{
	// A precondition, asserted in debug. In release a NaN becomes zero and an out-of-range value
	// saturates, so nothing reaches llround's undefined behaviour.
	REQUIRE(Length::from<Length::Metres>(std::numeric_limits<double>::quiet_NaN()) == Length{});
	REQUIRE(Length::from<Length::Metres>(std::numeric_limits<double>::infinity()).nanometres() ==
			std::numeric_limits<std::int64_t>::max());
	REQUIRE(Length::from<Length::Metres>(-1e30).nanometres() == std::numeric_limits<std::int64_t>::min());
}
#endif
