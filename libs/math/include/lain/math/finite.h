#pragma once

#include "lain/math/rigidtransform.h"
#include "lain/math/types.h"

#include <cmath>

// Whether a value holds only finite numbers: no NaN and no infinity in any component. What a caller
// asks of data it was handed before it computes with it. GLM's own isnan / isinf answer per
// component, and its isfinite is an experimental extension, so this says it once for the whole value.
namespace lain::math
{
	template <length_t N, typename T>
	bool isFinite(const Vec<N, T>& v)
	{
		for (length_t i = 0; i < N; ++i)
		{
			if (!std::isfinite(v[i]))
				return false;
		}
		return true;
	}

	template <typename T>
	bool isFinite(const Quat<T>& q)
	{
		return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
	}

	template <typename T>
	bool isFinite(const RigidTransform<T>& t)
	{
		return isFinite(t.rotation()) && isFinite(t.translation());
	}
} // namespace lain::math
