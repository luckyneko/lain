#pragma once

#include "lain/core/details/rounding.h"

#include <chrono>

namespace lain::core
{
	// A monotonic time value: internally an exact signed int64 nanosecond count
	// (std::chrono::nanoseconds — no float drift), but it reads as seconds and
	// serves as both a timestamp and an interval. Sourced from now() (steady_clock)
	// for frame deltas and performance timing. Wall-clock / calendar time
	// (formatting, ISO strings) is deferred to a future lain::core::DateTime — see
	// WORK.md; format a value you got there, not a monotonic Time.
	class Time
	{
	public:
		// Unit tags for from<>() / as<>() — double-based chrono durations.
		using Nanoseconds = std::chrono::duration<double, std::nano>;
		using Microseconds = std::chrono::duration<double, std::micro>;
		using Milliseconds = std::chrono::duration<double, std::milli>;
		using Seconds = std::chrono::duration<double>;
		using Minutes = std::chrono::duration<double, std::ratio<60>>;
		using Hours = std::chrono::duration<double, std::ratio<3600>>;

		constexpr Time() = default;

		// A high-resolution monotonic sample — the performance clock.
		static Time now()
		{
			return Time{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())};
		}

		// Build from a unit value, rounded to the nearest nanosecond: Time::from<Milliseconds>(16.6).
		// Rounded, not duration_cast: that truncates, so 1.001 ms (1000999.9999999999 ns as a double)
		// would be 1000999 ns.
		//
		// PRECONDITION: finite and within the representable range, as detail::roundToInt64 states, and
		// as core::Length's from<>() has. One that gets here anyway asserts in debug; in release a NaN
		// is zero and anything out of range saturates, where duration_cast would be undefined.
		template <class Units>
		static Time from(double value)
		{
			return Time{std::chrono::nanoseconds{
				detail::roundToInt64(std::chrono::duration<double, std::nano>(Units(value)).count())}};
		}

		// Read as a unit value: t.as<Milliseconds>().
		template <class Units>
		double as() const
		{
			return Units(m_ns).count(); // target rep is double, so the conversion is exact-typed
		}
		double seconds() const { return as<Seconds>(); }

		// std::chrono interop — implicit, so a Time passes to any chrono API.
		constexpr std::chrono::nanoseconds chrono() const { return m_ns; }
		constexpr operator std::chrono::nanoseconds() const { return m_ns; }

		// Elapsed since this sample (now() - *this).
		Time since() const { return now() - *this; }

		// Interval arithmetic (Time behaves as a duration).
		constexpr Time operator+(Time rhs) const { return Time{m_ns + rhs.m_ns}; }
		constexpr Time operator-(Time rhs) const { return Time{m_ns - rhs.m_ns}; }
		// The count is scaled and re-rounded, so a fractional factor does not truncate. No unit is
		// involved: scaling a quantity is not a reading of it.
		Time operator*(double scale) const
		{
			return Time{std::chrono::nanoseconds{detail::roundToInt64(static_cast<double>(m_ns.count()) * scale)}};
		}

		constexpr bool operator==(Time rhs) const { return m_ns == rhs.m_ns; }
		constexpr bool operator!=(Time rhs) const { return m_ns != rhs.m_ns; }
		constexpr bool operator<(Time rhs) const { return m_ns < rhs.m_ns; }
		constexpr bool operator<=(Time rhs) const { return m_ns <= rhs.m_ns; }
		constexpr bool operator>(Time rhs) const { return m_ns > rhs.m_ns; }
		constexpr bool operator>=(Time rhs) const { return m_ns >= rhs.m_ns; }

	private:
		explicit constexpr Time(std::chrono::nanoseconds ns)
			: m_ns(ns)
		{
		}

		std::chrono::nanoseconds m_ns{0}; // signed int64 ns: exact + safe differences
	};
} // namespace lain::core
