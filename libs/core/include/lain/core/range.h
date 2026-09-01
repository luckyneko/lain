#pragma once

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
	struct Range
	{
		std::size_t first = 0;
		std::size_t last = 0;
		std::size_t step = 1;

		// How many values this range visits. Zero for a step of 0 or a reversed pair — neither is
		// constructible through parse(), but a hand-built Range can hold them.
		std::size_t count() const;

		// Whether `value` is one of the values visited (not merely within the bounds).
		bool contains(std::size_t value) const;

		std::string toString() const; // "12" / "0-499" / "0-499x2"

		// Parse the literal form. Returns nullopt for anything that could not mean what it says:
		//   - a reversed pair ("10-2") — reversing is an operation on a collection, not a range;
		//   - a zero step ("0-9x0") — it would never terminate;
		//   - trailing junk, an empty string, a negative number;
		//   - a value too large to hold, rather than wrapping to a small one, because silently
		//     visiting the wrong values is worse than refusing.
		static std::optional<Range> parse(std::string_view text);
	};

	bool operator==(const Range& a, const Range& b);
	bool operator!=(const Range& a, const Range& b);

	// CLI11 converts a custom option type through an unqualified `lexical_cast` found by ADL on the
	// type. Satisfying it here is what lets an app write `cli.add_option("--frame", range)` with no
	// wrapper and no string staging — the same shape meta::enums gives an enum option.
	//
	// Note core names nothing of CLI11 to do this: the hook is a plain signature, so the dependency
	// stays one-way and this header remains std-only.
	bool lexical_cast(const std::string& input, Range& output);
} // namespace lain::core
