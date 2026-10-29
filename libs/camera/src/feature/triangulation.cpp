#include "lain/camera/feature/triangulation.h"

#include "angles.h"

#include <algorithm>
#include <cmath>

namespace lain::camera::feature
{
	// A normal-equations matrix this close to singular has rays parallel to the last bit: Σ(I − ddᵀ)
	// over two unit rays at an angle θ has determinant 2 sin²θ, so this is about 1e-10 rad of parallax,
	// far below any minimum angle a caller asks for.
	static constexpr double kSingular = 1e-20;

	static bool finite(const math::Vec3d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	Triangulation triangulate(const std::vector<Ray>& rays, double minimumAngle)
	{
		std::vector<Ray> usable;
		usable.reserve(rays.size());
		for (const Ray& r : rays)
		{
			const double length = math::length(r.direction);
			if (finite(r.centre) && finite(r.direction) && length > 0)
				usable.push_back({r.centre, r.direction / length});
		}

		Triangulation out;
		if (usable.size() < 2)
			return out; // TooFew
		for (std::size_t i = 0; i < usable.size(); ++i)
		{
			for (std::size_t j = i + 1; j < usable.size(); ++j)
				out.angle = std::max(out.angle, detail::angleBetween(usable[i].direction, usable[j].direction));
		}

		// Least squares over the perpendicular distances: Σ(I − ddᵀ) x = Σ(I − ddᵀ) c.
		math::Mat3d normal(0.0);
		math::Vec3d rhs(0.0);
		for (const Ray& r : usable)
		{
			const math::Mat3d away = math::Mat3d(1.0) - math::outerProduct(r.direction, r.direction);
			normal += away;
			rhs += away * r.centre;
		}
		if (!(out.angle >= minimumAngle) || !(math::determinant(normal) > kSingular))
		{
			out.status = TriangulationStatus::IllConditioned;
			return out;
		}
		out.point = math::inverse(normal) * rhs;
		out.status = TriangulationStatus::Ok;
		for (const Ray& r : usable)
		{
			if (!(math::dot(out.point - r.centre, r.direction) > 0))
			{
				out.status = TriangulationStatus::Behind;
				break;
			}
		}
		return out;
	}
} // namespace lain::camera::feature
