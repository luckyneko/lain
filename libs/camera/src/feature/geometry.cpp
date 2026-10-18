#include "lain/camera/feature/geometry.h"

#include "angles.h"
#include "lain/camera/feature/triangulation.h"

#include <lain/string/format.h>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace lain::camera::feature
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Five pairs fix a relative pose (the five-point problem); three point-rays fix an absolute pose up
	// to P3P's few solutions, and a fourth chooses among them. Eight pixel pairs fix a fundamental
	// matrix linearly (the eight-point problem); seven do up to three solutions. Four fix a homography.
	static constexpr std::size_t kRelativeMinimum = 5;
	static constexpr std::size_t kAbsoluteMinimum = 4;
	static constexpr std::size_t kFundamentalMinimum = 8;
	static constexpr std::size_t kHomographyMinimum = 4;

	static bool finite(const math::Vec3d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	static bool usable(const math::Vec3d& ray)
	{
		return finite(ray) && math::length(ray) > 0;
	}

	static bool finite(const math::Vec2d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y);
	}

	static bool positive(double v)
	{
		return std::isfinite(v) && v > 0;
	}

	// A matrix's entries in row-major order: F(row, column) is f[column][row] in GLM.
	static std::array<double, 9> entries(const math::Mat3d& f)
	{
		std::array<double, 9> out{};
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
				out[std::size_t(3 * row + column)] = f[column][row];
		}
		return out;
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

	// The same for a fundamental matrix or a homography, which are measured normalised: its entries in
	// row-major order.
	static std::array<double, 9> canonical(const math::Mat3d& f)
	{
		return entries(f);
	}

	// A matrix whose scale and sign mean nothing, normalised so neither can reach the canonical order:
	// unit Frobenius norm, its largest-magnitude entry positive (the first in row-major order, on a
	// tie). One that is not finite, or is zero, says nothing.
	static std::optional<math::Mat3d> normalised(const math::Mat3d& c)
	{
		const std::array<double, 9> e = entries(c);
		double norm = 0;
		std::size_t largest = 0;
		for (std::size_t i = 0; i < e.size(); ++i)
		{
			if (!std::isfinite(e[i]))
				return std::nullopt;
			norm += e[i] * e[i];
			if (std::abs(e[i]) > std::abs(e[largest]))
				largest = i;
		}
		norm = std::sqrt(norm);
		if (!(norm > 0) || !std::isfinite(norm))
			return std::nullopt;
		return c * ((e[largest] < 0 ? -1.0 : 1.0) / norm);
	}

	// Whether `x` (from candidate `px`) explains the input better than `y` (from `py`).
	template <typename Model>
	static bool better(const Measured& x, const Model& px, const Measured& y, const Model& py)
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

	// A fundamental matrix measured against pixel pairs, by Sampson distance: (x_bᵀ F x_a)² over the
	// summed squares of the first two entries of F x_a and of Fᵀ x_b, the first-order squared distance
	// from the pair to the matrix's epipolar geometry, in both images at once. A pair whose epipolar
	// lines both vanish (it sits on both epipoles) says nothing, and is no inlier.
	static Measured measureFundamental(const std::vector<PixelPair>& pairs, const math::Mat3d& f, double threshold)
	{
		Measured out;
		const math::Mat3d ft = math::transpose(f);
		for (std::size_t k = 0; k < pairs.size(); ++k)
		{
			const math::Vec3d xa{pairs[k].a.x, pairs[k].a.y, 1.0};
			const math::Vec3d xb{pairs[k].b.x, pairs[k].b.y, 1.0};
			const math::Vec3d lineB = f * xa;  // x_b's epipolar line in B
			const math::Vec3d lineA = ft * xb; // x_a's in A
			const double denominator = lineB.x * lineB.x + lineB.y * lineB.y + lineA.x * lineA.x + lineA.y * lineA.y;
			if (!(denominator > 0))
				continue;
			const double algebraic = math::dot(xb, lineB);
			const double squared = algebraic * algebraic / denominator;
			if (std::sqrt(squared) <= threshold)
			{
				out.inliers.push_back(std::uint32_t(k));
				out.sumSquared += squared;
				out.residuals += 1;
			}
		}
		return out;
	}

	// Where a homography carries a pixel, or nothing when it carries it to infinity.
	static std::optional<math::Vec2d> transfer(const math::Mat3d& h, const math::Vec2d& pixel)
	{
		const math::Vec3d x = h * math::Vec3d{pixel.x, pixel.y, 1.0};
		if (!(std::abs(x.z) > 0))
			return std::nullopt;
		const math::Vec2d out{x.x / x.z, x.y / x.z};
		if (!finite(out))
			return std::nullopt;
		return out;
	}

	// A homography measured against pixel pairs, by transfer distance both ways: A's pixel carried into
	// B against B's, and B's carried back by the inverse against A's. A pair is an inlier when both are
	// within the threshold, and both count as its residuals.
	static Measured measureHomography(const std::vector<PixelPair>& pairs, const math::Mat3d& h, double threshold)
	{
		Measured out;
		const math::Mat3d inverse = math::inverse(h);
		for (std::size_t k = 0; k < pairs.size(); ++k)
		{
			const std::optional<math::Vec2d> inB = transfer(h, pairs[k].a);
			const std::optional<math::Vec2d> inA = transfer(inverse, pairs[k].b);
			if (!inB || !inA)
				continue;
			const double forward = math::length(*inB - pairs[k].b);
			const double backward = math::length(*inA - pairs[k].a);
			if (forward <= threshold && backward <= threshold)
			{
				out.inliers.push_back(std::uint32_t(k));
				out.sumSquared += forward * forward + backward * backward;
				out.residuals += 2;
			}
		}
		return out;
	}

	// The shared shape of the facades: check the backend and the request, keep the usable entries, ask
	// the backend, measure every candidate, choose, and map the inliers back to the input. `prepare`
	// turns a candidate into the model to measure (or nullopt to skip it); `measure` measures it; and
	// `store` writes the chosen one and its RMS residual into the result.
	template <typename Result, typename Model, typename Entry, typename Usable, typename Propose, typename Prepare,
			  typename Measure, typename Store>
	static Result solve(const std::vector<Entry>& input, std::optional<std::string> requestProblem, std::size_t minimum,
						const char* entries, const char* problem, Usable isUsable, Propose propose, Prepare prepare,
						Measure measure, Store store)
	{
		const core::Time start = core::Time::now();
		Result out;
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
		if (requestProblem)
			return finish(Status::Unsupported, std::move(*requestProblem));

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
		const std::vector<Model> candidates = propose(*solver, usable);
		out.candidates = std::uint32_t(candidates.size());

		std::optional<Model> best;
		Measured bestMeasured;
		for (const Model& c : candidates)
		{
			const std::optional<Model> model = prepare(c);
			if (!model)
				continue;
			Measured m = measure(usable, *model);
			if (!best || better(m, *model, bestMeasured, *best))
			{
				best = model;
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

		for (const std::uint32_t k : bestMeasured.inliers)
			out.inliers.push_back(index[k]);
		store(out, *best, std::sqrt(bestMeasured.sumSquared / double(bestMeasured.residuals)));
		return finish(Status::Ok, {});
	}

	// The problem with an inlier angle, or nothing.
	static std::optional<std::string> angleProblem(const GeometryRequest& request)
	{
		if (positive(request.angle))
			return std::nullopt;
		return string::format("an inlier angle of {} is not positive", request.angle);
	}

	static void storePose(GeometryResult& out, const math::RigidTransformd& pose, double rms)
	{
		out.pose = pose;
		out.rmsAngle = rms;
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
		return solve<GeometryResult, math::RigidTransformd>(
			pairs, angleProblem(request), kRelativeMinimum, "pairs", "a relative pose",
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
			{ return measureRelative(usablePairs, bFromA, request.angle); },
			storePose);
	}

	GeometryResult absolutePose(const std::vector<PointRay>& pointRays, const GeometryRequest& request)
	{
		return solve<GeometryResult, math::RigidTransformd>(
			pointRays, angleProblem(request), kAbsoluteMinimum, "point-rays", "an absolute pose",
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
			{ return measureAbsolute(usableRays, cameraFromReference, request.angle); },
			storePose);
	}

	FundamentalResult fundamental(const std::vector<PixelPair>& pairs, const FundamentalRequest& request)
	{
		std::optional<std::string> problem;
		if (!positive(request.threshold))
			problem = string::format("an inlier distance of {} px is not positive", request.threshold);
		return solve<FundamentalResult, math::Mat3d>(
			pairs, std::move(problem), kFundamentalMinimum, "pixel pairs", "a fundamental matrix",
			[](const PixelPair& p) -> std::optional<PixelPair>
			{
				if (!finite(p.a) || !finite(p.b))
					return std::nullopt;
				return p;
			},
			[&request](const GeometrySolver& solver, const std::vector<PixelPair>& usablePairs)
			{ return solver.fundamentals(usablePairs, request.threshold, request.seed); },
			// Measured normalised, so a candidate's scale and sign cannot reach the canonical order.
			normalised,
			[&request](const std::vector<PixelPair>& usablePairs, const math::Mat3d& f)
			{ return measureFundamental(usablePairs, f, request.threshold); },
			[](FundamentalResult& out, const math::Mat3d& f, double rms)
			{
				out.matrix = f;
				out.rmsDistance = rms;
			});
	}

	HomographyResult homography(const std::vector<PixelPair>& pairs, const HomographyRequest& request)
	{
		std::optional<std::string> problem;
		if (!positive(request.threshold))
			problem = string::format("an inlier distance of {} px is not positive", request.threshold);
		return solve<HomographyResult, math::Mat3d>(
			pairs, std::move(problem), kHomographyMinimum, "pixel pairs", "a homography",
			[](const PixelPair& p) -> std::optional<PixelPair>
			{
				if (!finite(p.a) || !finite(p.b))
					return std::nullopt;
				return p;
			},
			[&request](const GeometrySolver& solver, const std::vector<PixelPair>& usablePairs)
			{ return solver.homographies(usablePairs, request.threshold, request.seed); },
			// Measured normalised, as a fundamental matrix is; one that cannot be inverted transfers
			// nothing back, so it is not measured.
			[](const math::Mat3d& c) -> std::optional<math::Mat3d>
			{
				const std::optional<math::Mat3d> h = normalised(c);
				if (!h)
					return std::nullopt;
				const double determinant = math::determinant(*h);
				if (!(std::abs(determinant) > 1e-12) || !std::isfinite(determinant))
					return std::nullopt;
				return h;
			},
			[&request](const std::vector<PixelPair>& usablePairs, const math::Mat3d& h)
			{ return measureHomography(usablePairs, h, request.threshold); },
			[](HomographyResult& out, const math::Mat3d& h, double rms)
			{
				out.matrix = h;
				out.rmsDistance = rms;
			});
	}
} // namespace lain::camera::feature
