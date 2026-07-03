#include "lain/core/time.h"

namespace lain::core
{
	Time Time::now()
	{
		return Time{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())};
	}

	Time Time::operator*(double scale) const
	{
		// Scale in seconds (double) so a fractional factor doesn't truncate the
		// nanosecond rep mid-multiply, then quantize back to exact ns.
		return from<Seconds>(seconds() * scale);
	}
} // namespace lain::core
