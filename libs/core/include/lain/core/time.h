#pragma once

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
		static Time now();

		// Build from a unit value: Time::from<Milliseconds>(16.6).
		template <class Units>
		static Time from(double value);
		// Read as a unit value: t.as<Milliseconds>().
		template <class Units>
		double as() const;
		double seconds() const { return as<Seconds>(); }

		// std::chrono interop — implicit, so a Time passes to any chrono API.
		constexpr std::chrono::nanoseconds chrono() const { return m_ns; }
		constexpr operator std::chrono::nanoseconds() const { return m_ns; }

		// Elapsed since this sample (now() - *this).
		Time since() const { return now() - *this; }

		// Interval arithmetic (Time behaves as a duration).
		constexpr Time operator+(Time rhs) const { return Time{m_ns + rhs.m_ns}; }
		constexpr Time operator-(Time rhs) const { return Time{m_ns - rhs.m_ns}; }
		Time operator*(double scale) const;

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

	template <class Units>
	Time Time::from(double value)
	{
		return Time{std::chrono::duration_cast<std::chrono::nanoseconds>(Units(value))};
	}

	template <class Units>
	double Time::as() const
	{
		return Units(m_ns).count(); // target rep is double, so the conversion is exact-typed
	}
} // namespace lain::core
