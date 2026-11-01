#include "lain/editor/playback.h"

#include <algorithm>
#include <cmath>

namespace lain::editor
{
	std::size_t Playback::advance(std::size_t position, std::size_t last, double rate, double seconds)
	{
		position = std::min(position, last);
		if (!m_playing || !std::isfinite(rate) || rate <= 0.0)
			return position;
		if (std::isfinite(seconds) && seconds > 0.0)
			m_carry += seconds;

		// Whole frames only, rounded DOWN: a frame is shown once its time has come, never before. Counted
		// as a double, so a long stall (a laptop woken after an hour) cannot overflow the count.
		const double step = 1.0 / rate;
		const double frames = std::floor(m_carry / step);
		if (frames < 1.0)
			return position;
		m_carry -= frames * step;

		const double count = double(last) + 1.0;
		if (!m_loop)
		{
			if (double(position) + frames >= double(last))
			{
				m_playing = false;
				m_carry = 0.0;
				return last;
			}
			return position + std::size_t(frames);
		}
		// Round, as many times as the time covers: only where it lands matters.
		return std::size_t(std::fmod(double(position) + std::fmod(frames, count), count));
	}
} // namespace lain::editor
