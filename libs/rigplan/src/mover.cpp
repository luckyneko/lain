#include "lain/rigplan/mover.h"

#include "lain/rigplan/realisation.h" // mountFromPlacement, the aim convention

#include <lain/string/format.h>

#include <algorithm>
#include <cmath>

namespace lain::rigplan
{
	static bool finite(const math::Vec3d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	static bool finite(const Aim& aim)
	{
		return std::isfinite(aim.panDegrees) && std::isfinite(aim.tiltDegrees) && std::isfinite(aim.rollDegrees);
	}

	std::vector<std::string> pathProblems(const Path& path)
	{
		std::vector<std::string> out;
		if (path.keys.empty())
			out.push_back("no keys, so nowhere to be");
		for (std::size_t i = 0; i < path.keys.size(); ++i)
		{
			const PathKey& key = path.keys[i];
			if (i > 0 && key.frame <= path.keys[i - 1].frame)
			{
				out.push_back(string::format("key {} is at frame {}, not after key {}'s frame {}", i, key.frame, i - 1,
											 path.keys[i - 1].frame));
			}
			if (!finite(key.position) || !finite(key.aim))
				out.push_back(string::format("key {} has a position or aim that is not finite", i));
		}
		return out;
	}

	math::RigidTransformd poseAt(const Path& path, std::size_t frame)
	{
		const std::vector<PathKey>& keys = path.keys;
		const auto pose = [](const PathKey& key)
		{ return mountFromPlacement(key.position, key.aim); };
		if (frame <= keys.front().frame)
			return pose(keys.front());
		if (frame >= keys.back().frame)
			return pose(keys.back());
		// The first key after `frame`, and the one before it.
		const auto after = std::upper_bound(keys.begin(), keys.end(), frame, [](std::size_t f, const PathKey& key)
											{ return f < key.frame; });
		const PathKey& a = *(after - 1);
		const PathKey& b = *after;
		if (frame == a.frame)
			return pose(a);
		const double t = double(frame - a.frame) / double(b.frame - a.frame);
		const math::RigidTransformd from = pose(a);
		const math::RigidTransformd to = pose(b);
		return math::RigidTransformd{math::slerp(from.rotation(), to.rotation(), t), math::mix(from.translation(), to.translation(), t)};
	}

	std::vector<std::string> boardProblems(const BoardMover& board)
	{
		std::vector<std::string> out;
		const camera::board::PatternResult pattern = camera::board::Pattern::create(board.pattern);
		for (const camera::board::PatternDiagnostic& d : pattern.diagnostics)
			out.push_back(string::format("its pattern: {}", d.detail));
		if (pattern.pattern)
		{
			for (const camera::board::SpecificationDiagnostic& d : camera::board::Specification::create(*pattern.pattern, board.instance).diagnostics)
				out.push_back(string::format("its instance: {}", d.detail));
		}
		if (!std::isfinite(board.margin) || board.margin < 0)
			out.push_back(string::format("a margin of {} m, which must be at least 0", board.margin));
		if (board.pixelsPerSquare == 0)
			out.push_back("no pixels a square, so no board to draw");
		if (!std::isfinite(board.backing) || board.backing <= 0)
			out.push_back(string::format("a backing of {} m, which must be positive", board.backing));
		for (const std::string& problem : pathProblems(board.path))
			out.push_back(string::format("its path: {}", problem));
		return out;
	}

	std::optional<camera::board::Specification> specificationOf(const BoardMover& board)
	{
		camera::board::PatternResult pattern = camera::board::Pattern::create(board.pattern);
		if (!pattern.pattern)
			return std::nullopt;
		return camera::board::Specification::create(*pattern.pattern, board.instance).specification;
	}

	math::RigidTransformd planFromBoard(const BoardMover& board, std::size_t frame)
	{
		const double square = board.instance.squareLength.value.metres();
		const math::Vec3d centre{board.pattern.squaresX * square / 2, board.pattern.squaresY * square / 2, 0.0};
		return poseAt(board.path, frame) * math::RigidTransformd{math::Quatd{1.0, 0.0, 0.0, 0.0}, -centre};
	}
} // namespace lain::rigplan
