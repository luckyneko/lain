#pragma once

#include <cmath>

namespace lain::camera::detail
{
	// A float parameter as the decimal a person typed, to six places. A float holds about seven
	// significant digits, so 0.7f is 0.699999988 and 0.2f is 0.200000003; rounding to millionths
	// gives back 0.7 and 0.2, which is also the resolution a pattern's fingerprint writes its marker
	// ratio at (board::Pattern::description).
	inline double decimal(float value)
	{
		return std::round(double(value) * 1e6) / 1e6;
	}
} // namespace lain::camera::detail
