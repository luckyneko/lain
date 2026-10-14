#pragma once

// What every residual of the Ceres refiner shares, held model or free: how a pixel error is
// whitened, and how a point in the reference frame is seen by a camera. Private to the plugin.
//
// The held residuals (ceresrefiner.cpp) and the free ones (freemodel.cpp) live in separate files on
// purpose. GCC limits how much inlining may grow a translation unit, and the twelve free-model cost
// functions, one per model for corners and for landmarks, are large. In one file with the held
// residuals they slowed the held path, which every registration runs: targetless registration's
// scale case took 32.3 s against 29.1 s before they existed, with the same 10,011 refinements, and
// 29.7 s once split (Release, linux-x86_64, three alternating rounds each; M9 slice 4, sub-slice 5).

#include <lain/camera/projection.h>
#include <lain/math/types.h>

#include <ceres/rotation.h>

namespace lain::camera::ceres
{
	// What a residual multiplies the pixel error by, so that it is in standard deviations: the inverse
	// of the covariance's Cholesky factor, [a 0; b c]. Every residual kind whitens through this one.
	struct Whitening
	{
		double a, b, c;
	};

	// A point in the reference frame seen by a camera: the pixel `projectInCamera` predicts, against
	// the pixel observed, whitened. False when the point does not project, which fails the solve. The
	// projection is camera::project<T> whether the model is held or free; only where its parameters
	// come from differs, so every residual kind goes through this one.
	template <typename T, typename Project>
	bool observe(const Project& projectInCamera, const T* const camera, const T* const inReference, const math::Vec2d& pixel,
				 const Whitening& w, T* residual)
	{
		T inCamera[3];
		::ceres::AngleAxisRotatePoint(camera, inReference, inCamera);
		for (int i = 0; i < 3; ++i)
			inCamera[i] += camera[3 + i];
		const Projection<T> predicted = projectInCamera(inCamera[0], inCamera[1], inCamera[2]);
		if (!predicted.ok())
			return false;
		const T du = predicted.u - T(pixel.x);
		const T dv = predicted.v - T(pixel.y);
		residual[0] = T(w.a) * du;
		residual[1] = T(w.b) * du + T(w.c) * dv;
		return true;
	}

	// A corner in its body's frame, posed into the reference frame by the body.
	template <typename T>
	void bodyToReference(const T* const body, const math::Vec3d& point, T* inReference)
	{
		const T corner[3] = {T(point.x), T(point.y), T(point.z)};
		::ceres::AngleAxisRotatePoint(body, corner, inReference);
		for (int i = 0; i < 3; ++i)
			inReference[i] += body[3 + i];
	}
} // namespace lain::camera::ceres
