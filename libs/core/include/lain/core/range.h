#pragma once

#include "lain/core/clioption.h" // LAIN_CLI_OPTION — the command-line conversion hook, below

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lain::core
{
	// An inclusive arithmetic range: `first` through `last`, every `step`-th value.
	//
	// Inclusive at BOTH ends, deliberately: a caller naming "0-499" means 500 things, and a
	// half-open range would make the common command-line spelling say one fewer than it reads.
	//
	// It carries its own literal form — "12", "0-499", "0-499x2" — the way core::Version carries
	// "1.4.0-rc.2". A value type that can be typed by a human is one a command line can accept
	// directly, which is the point: a cli option declares `core::Range` rather than a std::string
	// that some layer converts later, and the conversion cannot then drift from the type.
	//
	// THE INVARIANTS BELONG TO THE TYPE, not to each reader. A Range always satisfies
	// `last >= first` and `step >= 1`, so it always visits at least one value. That is what the
	// fields being private buys: count() and contains() answer without defending themselves, and a
	// sweep's `for (f = first(); f <= last(); f += step())` cannot fail to terminate. As a plain
	// aggregate the invariants held only for the ranges parse() built — every other reader had to
	// allow for the ones it did not, and a zero step silently meant an infinite loop.
	class Range
	{
	public:
		// The one-value range [0, 0].
		constexpr Range() = default;

		// `first` through `last` inclusive, every `step`-th value.
		//
		// PRECONDITION: `last >= first` and `step >= 1`. Both are programming errors rather than
		// data — parse() is the door untrusted text comes through, and it REFUSES them — so they
		// assert in debug and are normalised in release, rather than being carried into a range
		// that means nothing. (core is std-only, so this is <cassert>, not log::ensure.)
		Range(std::size_t first, std::size_t last, std::size_t step = 1);

		constexpr std::size_t first() const { return m_first; }
		constexpr std::size_t last() const { return m_last; }
		constexpr std::size_t step() const { return m_step; }

		// How many values this range visits — at least one, by the invariant.
		constexpr std::size_t count() const { return (m_last - m_first) / m_step + 1; }

		// Whether `value` is one of the values visited (not merely within the bounds).
		constexpr bool contains(std::size_t value) const
		{
			return value >= m_first && value <= m_last && (value - m_first) % m_step == 0;
		}

		std::string toString() const; // "12" / "0-499" / "0-499x2"

		// Parse the literal form. Returns nullopt for anything that could not mean what it says:
		//   - a reversed pair ("10-2") — reversing is an operation on a collection, not a range;
		//   - a zero step ("0-9x0") — it would never terminate;
		//   - trailing junk, an empty string, a negative number;
		//   - a value too large to hold, rather than wrapping to a small one, because silently
		//     visiting the wrong values is worse than refusing.
		//
		// Every one of those is checked BEFORE a Range is built, which is what keeps the
		// constructor's precondition a precondition: untrusted text cannot reach it.
		static std::optional<Range> parse(std::string_view text);

		constexpr bool operator==(const Range& other) const
		{
			return m_first == other.m_first && m_last == other.m_last && m_step == other.m_step;
		}
		constexpr bool operator!=(const Range& other) const { return !(*this == other); }

	private:
		std::size_t m_first = 0;
		std::size_t m_last = 0;
		std::size_t m_step = 1;
	};

	// A command-line option type: an app writes `cli.add_option("--frame", range)` with no wrapper
	// and no string staging, the same shape meta::enums gives an enum option. See clioption.h —
	// core names nothing of CLI11 to offer this.
	LAIN_CLI_OPTION(Range)
} // namespace lain::core
