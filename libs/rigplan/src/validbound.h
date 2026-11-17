#pragma once

#include <cmath>

// How lain::rigplan checks a bound, wherever it checks one: a specification's tolerances and read
// noise, a placement's tolerances, and the colours, shininess and intensities that may be zero but never
// negative. Private to lain::rigplan.
namespace lain::rigplan::detail
{
	// Whether `value` is finite and not negative.
	inline bool validBound(double value)
	{
		return std::isfinite(value) && value >= 0;
	}
} // namespace lain::rigplan::detail
