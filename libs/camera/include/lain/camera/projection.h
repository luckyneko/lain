#pragma once

#include "lain/camera/cameramodel.h"

#include <variant>

namespace lain::camera
{
	// A pixel: (0, 0) is the centre of the top-left pixel (ImageGeometry). Success says nothing about
	// whether the pixel is inside the image; ask contains() for that, since an off-image projection
	// is still a valid residual for a solver. A failed projection's coordinates are zero and mean
	// nothing (ProjectionStatus).
	template <typename T>
	struct Projection
	{
		ProjectionStatus status = ProjectionStatus::Ok;
		T u = T(0);
		T v = T(0);

		bool ok() const { return status == ProjectionStatus::Ok; }
	};

	// A camera model's projection is its distortion's, then its intrinsics; neither function knows
	// any distortion model, which carries its own algorithm (distortion.h). Both are SCALAR-GENERIC
	// for the reason the models are: one projection implementation, run by a solver's
	// automatic-differentiation scalar and by a report alike.

	// The pixel a camera-frame point (x, y, z) projects to.
	template <typename T>
	Projection<T> project(const CameraModel& model, T x, T y, T z)
	{
		const DistortedPoint<T> distorted = std::visit([&](const auto& d)
													   { return d.project(x, y, z, model.domain()); },
													   model.distortion());
		Projection<T> out;
		if (!distorted.ok())
		{
			out.status = distorted.status;
			return out;
		}
		const Intrinsics& k = model.intrinsics();
		out.u = T(k.fx) * distorted.x + T(k.cx);
		out.v = T(k.fy) * distorted.y + T(k.cy);
		return out;
	}

	// The ray through pixel (u, v).
	template <typename T>
	Unprojection<T> unproject(const CameraModel& model, T u, T v)
	{
		const Intrinsics& k = model.intrinsics();
		const T xd = (u - T(k.cx)) / T(k.fx);
		const T yd = (v - T(k.cy)) / T(k.fy);
		return std::visit([&](const auto& d)
						  { return d.unproject(xd, yd, model.domain()); }, model.distortion());
	}

	// Whether pixel (u, v) lies inside the model's image rectangle.
	bool contains(const CameraModel& model, double u, double v);
} // namespace lain::camera
