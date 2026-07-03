// Unit tests for lain::core::Time. Exact (int64-ns) storage with a seconds-facing
// API; no driver, no sleeps (monotonicity is checked across a little busywork).

#include "lain/core/time.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>

using lain::core::Time;

TEST_CASE("unit conversions go through the exact ns rep", "[time]")
{
	const Time t = Time::from<Time::Milliseconds>(1500.0);
	REQUIRE(t.seconds() == Catch::Approx(1.5));
	REQUIRE(t.as<Time::Seconds>() == Catch::Approx(1.5));
	REQUIRE(t.as<Time::Milliseconds>() == Catch::Approx(1500.0));
	REQUIRE(t.as<Time::Microseconds>() == Catch::Approx(1.5e6));
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
