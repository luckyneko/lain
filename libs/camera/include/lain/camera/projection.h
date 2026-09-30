#pragma once

#include "lain/camera/cameramodel.h"

namespace lain::camera
{
	// The outcome of projecting or unprojecting through a camera model (CONTEXT.md, "Projection
	// status"). Failure is a status, never a NaN and never an exception; a failed result's
	// coordinates are zero and mean nothing.
	enum class ProjectionStatus
	{
		Ok,
		BehindCamera,  // the point is not in front of the camera (Z <= 0)
		OutsideDomain, // past where the model's distortion is defined (see DistortionDomain)
		NotConverged,  // the iterative inverse of the distortion did not settle
	};

	// A pixel: (0, 0) is the centre of the top-left pixel (ImageGeometry). Success says nothing about
	// whether the pixel is inside the image; ask contains() for that, since an off-image projection
	// is still a valid residual for a solver.
	template <typename T>
	struct Projection
	{
		ProjectionStatus status = ProjectionStatus::Ok;
		T u = T(0);
		T v = T(0);

		bool ok() const { return status == ProjectionStatus::Ok; }
	};

	// A ray: the UNIT direction, in the camera frame, of every point that projects to the pixel. Its
	// z is always positive when the status is Ok.
	template <typename T>
	struct Unprojection
	{
		ProjectionStatus status = ProjectionStatus::Ok;
		T x = T(0);
		T y = T(0);
		T z = T(0);

		bool ok() const { return status == ProjectionStatus::Ok; }
	};

	// SCALAR-GENERIC, so an automatic-differentiation scalar (a Ceres Jet) runs this same code. That
	// is the rule these exist for: one projection implementation, which calibration, validation and
	// later refinement all use, so what a solver minimises is what a report measures. The model's
	// parameters stay double; only the point is T.

	// The pixel a camera-frame point (x, y, z) projects to.
	template <typename T>
	Projection<T> project(const CameraModel& model, T x, T y, T z);

	// The ray through pixel (u, v).
	template <typename T>
	Unprojection<T> unproject(const CameraModel& model, T u, T v);

	// Whether pixel (u, v) lies inside the model's image rectangle.
	bool contains(const CameraModel& model, double u, double v);
} // namespace lain::camera

#include "lain/camera/details/projection.inl"
