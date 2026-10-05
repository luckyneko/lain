#include "lain/core/details/rounding.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace lain::core::detail
{
	std::int64_t roundToInt64(double value)
	{
		assert(std::isfinite(value) && "a quantity must be finite: refuse NaN and infinity where data enters");
		if (std::isnan(value))
			return 0;
		// 2^63 is exactly representable as a double and is the first value beyond int64, so anything
		// at or past it saturates rather than overflowing llround.
		constexpr double kLimit = 9223372036854775808.0;
		assert(std::abs(value) < kLimit && "a quantity must fit in an int64 count");
		if (value >= kLimit)
			return std::numeric_limits<std::int64_t>::max();
		if (value <= -kLimit)
			return std::numeric_limits<std::int64_t>::min();
		return std::llround(value);
	}
} // namespace lain::core::detail
