#pragma once

#include <lain/math/types.h>

#include <cmath>

// How lain::camera measures the angle between two directions, wherever it measures one: the
// geometry facades, triangulation and every registration method. Private to lain::camera.
namespace lain::camera::detail
{
	// The angle between `u` and `v`, radians, in [0, pi]: atan2 of the cross product's length and the
	// dot product, which keeps its precision for the tiny angles residuals are measured in, where acos
	// does not. Neither need be unit.
	inline double angleBetween(const math::Vec3d& u, const math::Vec3d& v)
	{
		return std::atan2(math::length(math::cross(u, v)), math::dot(u, v));
	}
} // namespace lain::camera::detail
