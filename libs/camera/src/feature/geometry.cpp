#include "lain/camera/feature/geometry.h"

#include "angles.h"
#include "lain/camera/feature/triangulation.h"

#include <lain/string/format.h>

#include <cmath>
#include <memory>
#include <tuple>
#include <utility>

namespace lain::camera::feature
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Five pairs fix a relative pose (the five-point problem); three point-rays fix an absolute pose up
	// to P3P's few solutions, and a fourth chooses among them.
	static constexpr std::size_t kRelativeMinimum = 5;
	static constexpr std::size_t kAbsoluteMinimum = 4;

	static bool finite(const math::Vec3d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	static bool usable(const math::Vec3d& ray)
	{
		return finite(ray) && math::length(ray) > 0;
	}

	static bool finite(const math::RigidTransformd& t)
	{
		const math::Quatd& q = t.rotation();
		return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
			   finite(t.translation());
	}

	// How well one candidate explains the input: its inliers (indices into the usable entries) and the
	// sum of their squared residuals.
	struct Measured
	{
		std::vector<std::uint32_t> inliers;
		double sumSquared = 0;
		std::uint32_t residuals = 0; // rays summed over: two per pair, one per point-ray
	};

	// A total order on poses that does not depend on where they came from: the quaternion with w ≥ 0
	// (q and −q are one rotation), then the translation. The last word between candidates that tie on
	// everything a measurement can see, so the choice never depends on the backend's order.
	static auto canonical(const math::RigidTransformd& t)
	{
		const math::Quatd& q = t.rotation();
		const double s = q.w < 0 ? -1.0 : 1.0;
		const math::Vec3d& p = t.translation();
		return std::make_tuple(s * q.w, s * q.x, s * q.y, s * q.z, p.x, p.y, p.z);
	}

	// Whether `x` (posed by `px`) explains the input better than `y` (posed by `py`).
	static bool better(const Measured& x, const math::RigidTransformd& px, const Measured& y,
					   const math::RigidTransformd& py)
	{
		if (x.inliers.size() != y.inliers.size())
			return x.inliers.size() > y.inliers.size();
		if (x.sumSquared != y.sumSquared)
			return x.sumSquared < y.sumSquared;
		return canonical(px) < canonical(py);
	}

	// One correspondence under a relative pose: triangulated from A's ray and B's ray turned into A's
	// frame. Below the parallax floor (`angle`) it is a point at infinity, whose residual is half the
	// angle between the rays; behind either camera it is no inlier at all.
	static Measured measureRelative(const std::vector<RayPair>& pairs, const math::RigidTransformd& bFromA, double angle)
	{
		Measured out;
		const math::Quatd aFromBRotation = math::conjugate(bFromA.rotation());
		const math::Vec3d centreB = bFromA.inverse().translation(); // B's centre in A's frame
		for (std::size_t k = 0; k < pairs.size(); ++k)
		{
			const math::Vec3d rayA = pairs[k].a;
			const math::Vec3d rayB = aFromBRotation * pairs[k].b;
			const Triangulation t = triangulate({{math::Vec3d{0.0}, rayA}, {centreB, rayB}}, angle);
			double residualA = 0;
			double residualB = 0;
			if (t.status == TriangulationStatus::Ok)
			{
				residualA = detail::angleBetween(rayA, t.point);
				residualB = detail::angleBetween(rayB, t.point - centreB);
			}
			else if (t.status == TriangulationStatus::IllConditioned)
				residualA = residualB = detail::angleBetween(rayA, rayB) / 2;
			else
				continue; // Behind: cheirality
			if (residualA <= angle && residualB <= angle)
			{
				out.inliers.push_back(std::uint32_t(k));
				out.sumSquared += residualA * residualA + residualB * residualB;
				out.residuals += 2;
			}
		}
		return out;
	}

	// One point-ray under an absolute pose: the ray against the direction to the posed point. A point
	// behind the camera is more than a right angle away, so never an inlier.
	static Measured measureAbsolute(const std::vector<PointRay>& pointRays, const math::RigidTransformd& cameraFromReference,
									double angle)
	{
		Measured out;
		for (std::size_t k = 0; k < pointRays.size(); ++k)
		{
			const double residual = detail::angleBetween(pointRays[k].ray, cameraFromReference.apply(pointRays[k].point));
			if (residual <= angle)
			{
				out.inliers.push_back(std::uint32_t(k));
				out.sumSquared += residual * residual;
				out.residuals += 1;
			}
		}
		return out;
	}

	// The shared shape of both facades: check the backend and the request, keep the usable entries,
	// ask the backend, measure every candidate, choose, and map the inliers back to the input.
	// `prepare` turns a candidate into the pose to measure (or nullopt to skip it); `measure` measures it.
	template <typename Entry, typename Usable, typename Propose, typename Prepare, typename Measure>
	static GeometryResult solve(const std::vector<Entry>& input, const GeometryRequest& request, std::size_t minimum,
								const char* entries, const char* problem, Usable isUsable, Propose propose, Prepare prepare,
								Measure measure)
	{
		const core::Time start = core::Time::now();
		GeometryResult out;
		const auto finish = [&](Status status, std::string detail)
		{
			out.status = status;
			out.detail = std::move(detail);
			out.elapsed = core::Time::now() - start;
			return std::move(out);
		};

		const std::vector<std::string> backends = geometryRegistry().keys();
		if (backends.empty())
			return finish(Status::NoBackend, "this build has no geometry solver (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!(std::isfinite(request.angle) && request.angle > 0))
			return finish(Status::Unsupported, string::format("an inlier angle of {} is not positive", request.angle));

		std::vector<Entry> usable;
		std::vector<std::uint32_t> index; // usable entry -> input index
		for (std::size_t k = 0; k < input.size(); ++k)
		{
			std::optional<Entry> entry = isUsable(input[k]);
			if (entry)
			{
				usable.push_back(*entry);
				index.push_back(std::uint32_t(k));
			}
		}
		if (usable.size() < minimum)
		{
			return finish(Status::TooFew,
						  string::format("{} usable {}; {} needs {}", usable.size(), entries, problem, minimum));
		}

		const std::unique_ptr<GeometrySolver> solver = geometryRegistry().create(backends.front());
		out.provenance = solver->provenance();
		const std::vector<math::RigidTransformd> candidates = propose(*solver, usable);
		out.candidates = std::uint32_t(candidates.size());

		std::optional<math::RigidTransformd> best;
		Measured bestMeasured;
		for (const math::RigidTransformd& c : candidates)
		{
			const std::optional<math::RigidTransformd> pose = prepare(c);
			if (!pose)
				continue;
			Measured m = measure(usable, *pose);
			if (!best || better(m, *pose, bestMeasured, *best))
			{
				best = pose;
				bestMeasured = std::move(m);
			}
		}
		if (!best || bestMeasured.inliers.size() < minimum)
		{
			return finish(Status::NoSolution,
						  string::format("the best of {} candidates explains {} of {} usable {}; {} needs {}",
										 candidates.size(), best ? bestMeasured.inliers.size() : 0, usable.size(), entries,
										 problem, minimum));
		}

		out.pose = best;
		for (const std::uint32_t k : bestMeasured.inliers)
			out.inliers.push_back(index[k]);
		out.rmsAngle = std::sqrt(bestMeasured.sumSquared / double(bestMeasured.residuals));
		return finish(Status::Ok, {});
	}

	// --- the registry and the facades ---------------------------------------------------

	core::Factory<GeometrySolver>& geometryRegistry()
	{
		static core::Factory<GeometrySolver> registry;
		return registry;
	}

	bool canSolveGeometry()
	{
		return !geometryRegistry().keys().empty();
	}

	GeometryResult relativePose(const std::vector<RayPair>& pairs, const GeometryRequest& request)
	{
		return solve(
			pairs, request, kRelativeMinimum, "pairs", "a relative pose",
			[](const RayPair& p) -> std::optional<RayPair>
			{
				if (!usable(p.a) || !usable(p.b))
					return std::nullopt;
				return RayPair{math::normalize(p.a), math::normalize(p.b)};
			},
			[&request](const GeometrySolver& solver, const std::vector<RayPair>& usablePairs)
			{ return solver.relativePoses(usablePairs, request.angle, request.seed); },
			// A candidate with no baseline places nothing, and the translation's length means nothing.
			[](const math::RigidTransformd& c) -> std::optional<math::RigidTransformd>
			{
				const double length = math::length(c.translation());
				if (!finite(c) || !(length > 0))
					return std::nullopt;
				return math::RigidTransformd{c.rotation(), c.translation() / length};
			},
			[&request](const std::vector<RayPair>& usablePairs, const math::RigidTransformd& bFromA)
			{ return measureRelative(usablePairs, bFromA, request.angle); });
	}

	GeometryResult absolutePose(const std::vector<PointRay>& pointRays, const GeometryRequest& request)
	{
		return solve(
			pointRays, request, kAbsoluteMinimum, "point-rays", "an absolute pose",
			[](const PointRay& p) -> std::optional<PointRay>
			{
				if (!finite(p.point) || !usable(p.ray))
					return std::nullopt;
				return PointRay{p.point, math::normalize(p.ray)};
			},
			[&request](const GeometrySolver& solver, const std::vector<PointRay>& usableRays)
			{ return solver.absolutePoses(usableRays, request.angle, request.seed); },
			[](const math::RigidTransformd& c) -> std::optional<math::RigidTransformd>
			{
				if (!finite(c))
					return std::nullopt;
				return c;
			},
			[&request](const std::vector<PointRay>& usableRays, const math::RigidTransformd& cameraFromReference)
			{ return measureAbsolute(usableRays, cameraFromReference, request.angle); });
	}
} // namespace lain::camera::feature
