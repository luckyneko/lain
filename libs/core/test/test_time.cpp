// Unit tests for lain::core::Time. Exact (int64-ns) storage with a seconds-facing
// API; no driver, no sleeps (monotonicity is checked across a little busywork).

#include "lain/core/time.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>

using lain::core::Time;

TEST_CASE("unit conversions go through the exact ns rep", "[time]")
{
	const Time t = Time::from<Time::Milliseconds>(1500.0);
	REQUIRE(t.seconds() == Catch::Approx(1.5));
	REQUIRE(t.as<Time::Seconds>() == Catch::Approx(1.5));
	REQUIRE(t.as<Time::Milliseconds>() == Catch::Approx(1500.0));
	REQUIRE(t.as<Time::Microseconds>() == Catch::Approx(1.5e6));
}

TEST_CASE("a unit value rounds to the nearest nanosecond", "[time]")
{
	// 1.001 ms is 1000999.9999999999 ns as a double product, so truncating loses a nanosecond: it
	// does for 1.5% of the whole microseconds below 100 ms.
	REQUIRE(Time::from<Time::Milliseconds>(1.001).chrono().count() == 1'001'000);
	// A frame at 24 fps is 41666666.67 ns, which rounds up.
	REQUIRE(Time::from<Time::Seconds>(1.0 / 24.0).chrono().count() == 41'666'667);
	REQUIRE(Time::from<Time::Seconds>(1.4e-9).chrono().count() == 1);
	REQUIRE(Time::from<Time::Seconds>(-1.6e-9).chrono().count() == -2);
	// Scaling re-rounds the count rather than truncating it.
	REQUIRE((Time::from<Time::Seconds>(1.0) * (1.0 / 24.0)).chrono().count() == 41'666'667);
}

TEST_CASE("Time behaves as a duration under arithmetic", "[time]")
{
	const Time a = Time::from<Time::Seconds>(2.0);
	const Time b = Time::from<Time::Seconds>(0.5);

	REQUIRE((a - b).seconds() == Catch::Approx(1.5));
	REQUIRE((a + b).seconds() == Catch::Approx(2.5));
	REQUIRE((b * 4.0).seconds() == Catch::Approx(2.0));
	REQUIRE(b < a);
	REQUIRE((a - a) == Time{});
}

TEST_CASE("a difference can be negative (signed rep)", "[time]")
{
	const Time a = Time::from<Time::Seconds>(0.25);
	const Time b = Time::from<Time::Seconds>(1.0);
	REQUIRE((a - b).seconds() == Catch::Approx(-0.75));
}

TEST_CASE("chrono interop is implicit and lossless to ns", "[time]")
{
	const Time t = Time::from<Time::Milliseconds>(250.0);

	const std::chrono::nanoseconds ns = t; // implicit conversion
	REQUIRE(ns.count() == 250'000'000);
	REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(t.chrono()).count() == 250);
}

TEST_CASE("now() is monotonic and since() measures forward", "[time]")
{
	const Time t0 = Time::now();

	volatile uint64_t acc = 0;
	for (uint64_t i = 0; i < 2'000'000; ++i)
		acc += i;
	(void)acc;

	const Time t1 = Time::now();
	REQUIRE(t1 >= t0);
	REQUIRE(t0.since().seconds() >= 0.0);
}

#ifdef NDEBUG
TEST_CASE("an invalid unit value is made defined in release, never undefined", "[time]")
{
	// A precondition, asserted in debug, as core::Length's is. In release a NaN becomes zero and an
	// out-of-range value saturates, where duration_cast's float-to-int conversion is undefined.
	REQUIRE(Time::from<Time::Seconds>(std::numeric_limits<double>::quiet_NaN()) == Time{});
	REQUIRE(Time::from<Time::Seconds>(std::numeric_limits<double>::infinity()).chrono().count() ==
			std::numeric_limits<std::int64_t>::max());
	REQUIRE(Time::from<Time::Hours>(-1e30).chrono().count() == std::numeric_limits<std::int64_t>::min());
}
#endif
