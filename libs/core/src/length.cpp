#include "lain/core/length.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace lain::core
{
	std::int64_t Length::toNanometres(double value, double scale)
	{
		const double nanometres = value * scale;
		assert(std::isfinite(nanometres) && "a Length must be finite: refuse NaN and infinity where data enters");
		if (std::isnan(nanometres))
			return 0;
		// 2^63 is exactly representable as a double and is the first value beyond int64, so anything
		// at or past it saturates rather than overflowing llround.
		constexpr double kLimit = 9223372036854775808.0;
		assert(std::abs(nanometres) < kLimit && "a Length must fit in int64 nanometres");
		if (nanometres >= kLimit)
			return std::numeric_limits<std::int64_t>::max();
		if (nanometres <= -kLimit)
			return std::numeric_limits<std::int64_t>::min();
		return std::llround(nanometres);
	}
} // namespace lain::core
