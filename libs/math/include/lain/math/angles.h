#pragma once

#include "lain/math/rigidtransform.h"
#include "lain/math/types.h"

#include <cmath>

// Angles between directions and between rotations, measured so they keep their precision near zero,
// where residuals and stability are measured: atan2 of a sine against a cosine, never acos of a
// cosine. acos turns the largest double below 1 into 1.5e-8 rad, so an acos-based angle cannot tell
// 1e-9 rad from 0.
namespace lain::math
{
	// The angle between `u` and `v`, radians, in [0, pi]. Neither need be unit.
	template <typename T>
	T angleBetween(const Vec3<T>& u, const Vec3<T>& v)
	{
		return std::atan2(length(cross(u, v)), dot(u, v));
	}

	// The angle of the rotation taking `a` to `b`, radians, in [0, pi]: 2 atan2(|v|, |w|) of the
	// relative quaternion. q and -q are one rotation, so it is the shorter way round. Neither need be
	// unit, since the ratio is all that counts.
	template <typename T>
	T rotationBetween(const Quat<T>& a, const Quat<T>& b)
	{
		const Quat<T> relative = conjugate(a) * b;
		const T v = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
		return T(2) * std::atan2(v, std::abs(relative.w));
	}

	// The angle between two transforms' rotations; their translations play no part.
	template <typename T>
	T rotationBetween(const RigidTransform<T>& a, const RigidTransform<T>& b)
	{
		return rotationBetween(a.rotation(), b.rotation());
	}
} // namespace lain::math
