#pragma once

#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <cmath>

// How lain::camera measures the angle between two directions, and between two rotations, wherever it
// measures one: the geometry facades, triangulation, every registration method and rig comparison.
// Private to lain::camera.
namespace lain::camera::detail
{
	// The angle between `u` and `v`, radians, in [0, pi]: atan2 of the cross product's length and the
	// dot product, which keeps its precision for the tiny angles residuals are measured in, where acos
	// does not. Neither need be unit.
	inline double angleBetween(const math::Vec3d& u, const math::Vec3d& v)
	{
		return std::atan2(math::length(math::cross(u, v)), math::dot(u, v));
	}

	// The rotation between two transforms, radians: 2 atan2(|v|, |w|) of the relative quaternion,
	// which keeps its precision for the tiny angles stability is measured in, where acos does not.
	inline double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd relative = math::conjugate(a.rotation()) * b.rotation();
		const double v = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
		return 2.0 * std::atan2(v, std::abs(relative.w));
	}
} // namespace lain::camera::detail
