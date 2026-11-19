#pragma once

#include "lain/camera/cameramodel.h"

#include <array>
#include <type_traits>
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

	// The pixel a camera-frame point (x, y, z) projects to, from a model's PARAMETERS rather than a
	// model: the intrinsics in Intrinsics' order (fx, fy, cx, cy) and the distortion's coefficients in
	// its own order, both as scalar S, with the point and the result as T (distortion.h). This is what
	// a solver estimating a model differentiates (S = T, a Jet); a held model runs it at S = double
	// through the overload below, which is this one with the model's own doubles. `Model` is named,
	// since an array cannot say which model it belongs to, and `domain` is the starting model's,
	// since within one solve a model's domain does not move (ADR-0017).
	template <typename Model, typename S, typename T>
	Projection<T> project(const std::array<S, 4>& intrinsics, const typename Model::template Coefficients<S>& coefficients,
						  const DistortionDomain& domain, T x, T y, T z)
	{
		const auto& [fx, fy, cx, cy] = intrinsics;
		const DistortedPoint<T> distorted = Model::project(coefficients, x, y, z, domain);
		Projection<T> out;
		if (!distorted.ok())
		{
			out.status = distorted.status;
			return out;
		}
		out.u = T(fx) * distorted.x + T(cx);
		out.v = T(fy) * distorted.y + T(cy);
		return out;
	}

	// The pixel a camera-frame point (x, y, z) projects to: the parameter overload above, with this
	// model's doubles. One implementation, two doors.
	template <typename T>
	Projection<T> project(const CameraModel& model, T x, T y, T z)
	{
		const Intrinsics& k = model.intrinsics();
		const std::array<double, 4> intrinsics = {k.fx, k.fy, k.cx, k.cy};
		return std::visit(
			[&](const auto& d)
			{
				using Model = std::decay_t<decltype(d)>;
				return project<Model>(intrinsics, d.coefficients(), model.domain(), x, y, z);
			},
			model.distortion());
	}

	// The ray through pixel (u, v). It has no parameter overload: nothing differentiates an
	// unprojection, so one would be an entry point with no caller. Its formulas are generic in the
	// coefficients' scalar anyway, since projection shares them.
	template <typename T>
	Unprojection<T> unproject(const CameraModel& model, T u, T v)
	{
		const Intrinsics& k = model.intrinsics();
		const T xd = (u - T(k.cx)) / T(k.fx);
		const T yd = (v - T(k.cy)) / T(k.fy);
		return std::visit([&](const auto& d)
						  { return d.unproject(d.coefficients(), xd, yd, model.domain()); }, model.distortion());
	}

	// Whether pixel (u, v) lies inside the model's image rectangle.
	bool contains(const CameraModel& model, double u, double v);
} // namespace lain::camera
