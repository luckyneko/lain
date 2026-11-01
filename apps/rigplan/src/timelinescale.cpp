#include "timelinescale.h"

#include <algorithm>
#include <cmath>

namespace rigplan
{
	namespace rp = lain::rigplan;

	double TimelineScale::slot() const
	{
		return std::max(0.0, width - overflow) / double(counted());
	}

	double TimelineScale::xOf(std::size_t frame) const
	{
		if (beyond(frame))
			return left + width - overflow * 0.5;
		return left + (double(frame) + 0.5) * slot();
	}

	std::size_t TimelineScale::frameAt(double x) const
	{
		const double s = slot();
		if (!(s > 0.0) || !(x > left))
			return 0;
		// The slot x falls in, rounded down: frame f is [left + f s, left + (f + 1) s), so the last frame's
		// slot runs right up to the strip and is never a sliver at the edge.
		const double f = std::floor((x - left) / s);
		const double last = double(counted() - 1);
		return std::size_t(std::min(f, last));
	}

	static bool beyond(const rp::Path& path, std::size_t frames)
	{
		return std::any_of(path.keys.begin(), path.keys.end(), [&](const rp::PathKey& key)
						   { return key.frame >= frames; });
	}

	bool keysBeyond(const rp::RigPlan& plan)
	{
		const std::size_t frames = std::max<std::size_t>(1, plan.timeline.frames);
		for (const rp::BoardMover& board : plan.boards)
		{
			if (beyond(board.path, frames))
				return true;
		}
		for (const rp::Prop& prop : plan.props)
		{
			if (prop.path && beyond(*prop.path, frames))
				return true;
		}
		return false;
	}

	std::optional<std::size_t> nearestKey(const TimelineScale& scale, const rp::Path& path, double x, double radius)
	{
		std::optional<std::size_t> nearest;
		double best = radius;
		for (std::size_t k = 0; k < path.keys.size(); ++k)
		{
			const double distance = std::abs(scale.xOf(path.keys[k].frame) - x);
			// Strictly nearer, so a tie keeps the earlier key; and within the radius, which `best` starts at.
			if (distance < best || (!nearest && distance <= radius))
			{
				nearest = k;
				best = distance;
			}
		}
		return nearest;
	}
} // namespace rigplan
