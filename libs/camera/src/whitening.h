#pragma once

#include "lain/camera/refinement/refiner.h"

#include <lain/math/types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

// How lain::camera whitens a pixel residual, wherever it judges one: an outlier in every registration
// and calibration method is an observation whose whitened residual exceeds the loss scale. Private to
// lain::camera.
namespace lain::camera::detail
{
	// The squared length of the pixel residual `r` whitened: by the Cholesky factor of `covariance`
	// (xx, xy, yy) when there is one, L^-1 r, else by the noise model's sigma.
	inline double whitenedSquared(const math::Vec2d& r, const std::optional<std::array<double, 3>>& covariance,
								  const refinement::NoiseModel& noise)
	{
		if (!covariance)
			return math::dot(r, r) / (noise.pixelSigma * noise.pixelSigma);
		const auto& c = *covariance;
		const double l11 = std::sqrt(c[0]);
		const double l21 = c[1] / l11;
		const double l22 = std::sqrt(std::max(c[2] - l21 * l21, 0.0));
		const double w1 = r.x / l11;
		const double w2 = (r.y - l21 * w1) / l22;
		return w1 * w1 + w2 * w2;
	}
} // namespace lain::camera::detail
