#pragma once

#include <cstdint>

namespace lain::core::detail
{
	// `value` rounded to the nearest integer, the one door from a double into a quantity stored as an
	// exact int64 count: core::Length's nanometres and core::Time's nanoseconds. One routine, so the
	// two cannot disagree about what a half rounds to or what happens at the edges.
	//
	// PRECONDITION: finite and within int64. A NaN or an infinity is a programming error, not data:
	// whatever turns untrusted input into a quantity checks it and refuses it first. So it asserts in
	// debug, and in release it is made defined rather than undefined, NaN becoming zero and anything
	// out of range saturating. core is std-only, so this is <cassert>, as core::Range does.
	std::int64_t roundToInt64(double value);
} // namespace lain::core::detail
