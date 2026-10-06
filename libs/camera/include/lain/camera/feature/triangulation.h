#pragma once

#include <lain/math/types.h>

#include <vector>

// Where rays meet, measured by lain rather than by a backend: the relative-pose facade scores its
// candidates with it, and targetless registration places and validates landmarks with it
// (ADR-0016, "Producers propose; lain decides").
namespace lain::camera::feature
{
	// A ray from `centre` along `direction`, in some frame every ray of one triangulation shares.
	struct Ray
	{
		math::Vec3d centre{0.0};
		math::Vec3d direction{0.0, 0.0, 1.0};
	};

	enum class TriangulationStatus
	{
		Ok,
		TooFew,			// fewer than two usable rays
		IllConditioned, // the rays are too close to parallel for a depth: no two differ by the minimum angle
		Behind,			// the nearest point lies behind at least one ray's centre
	};

	struct Triangulation
	{
		TriangulationStatus status = TriangulationStatus::TooFew;
		math::Vec3d point{0.0}; // meaningful when Ok or Behind
		double angle = 0;		// the largest angle between two of the rays, radians: the parallax

		bool ok() const { return status == TriangulationStatus::Ok; }
	};

	// The point nearest to every ray in least squares (the sum of squared perpendicular distances,
	// which for two rays is the midpoint of their common perpendicular). Directions need not be unit;
	// a ray with a zero or non-finite direction or centre is ignored. IllConditioned when the parallax
	// is below `minimumAngle`, since the depth along nearly parallel rays is set by noise, or when the
	// rays are exactly parallel.
	Triangulation triangulate(const std::vector<Ray>& rays, double minimumAngle);
} // namespace lain::camera::feature
