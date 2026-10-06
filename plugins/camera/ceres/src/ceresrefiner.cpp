#include "ceresrefiner.h"

#include <lain/camera/projection.h>
#include <lain/core/time.h>

#include <ceres/autodiff_cost_function.h>
#include <ceres/loss_function.h>
#include <ceres/ordered_groups.h>
#include <ceres/problem.h>
#include <ceres/rotation.h>
#include <ceres/solver.h>
#include <ceres/types.h>
#include <ceres/version.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace lain::camera::ceres
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A rigid transform as Ceres optimises it: angle-axis rotation, then translation.
	using Pose = std::array<double, 6>;

	static Pose poseOf(const math::RigidTransformd& transform)
	{
		const math::Quatd& q = transform.rotation();
		const double quaternion[4] = {q.w, q.x, q.y, q.z};
		Pose out{};
		::ceres::QuaternionToAngleAxis(quaternion, out.data());
		out[3] = transform.translation().x;
		out[4] = transform.translation().y;
		out[5] = transform.translation().z;
		return out;
	}

	static math::RigidTransformd transformOf(const Pose& pose)
	{
		double quaternion[4];
		::ceres::AngleAxisToQuaternion(pose.data(), quaternion);
		return math::RigidTransformd{math::Quatd{quaternion[0], quaternion[1], quaternion[2], quaternion[3]},
									 math::Vec3d{pose[3], pose[4], pose[5]}};
	}

	// What a residual multiplies the pixel error by, so that it is in standard deviations: the inverse
	// of the covariance's Cholesky factor, [a 0; b c]. Every residual kind whitens through this one.
	struct Whitening
	{
		double a, b, c;
	};

	// The measured covariance's whitening, or the noise model's sigma's with none; nothing when the
	// covariance is not positive definite.
	static std::optional<Whitening> whitening(const std::optional<std::array<double, 3>>& covariance, double sigma)
	{
		if (!covariance)
			return Whitening{1.0 / sigma, 0.0, 1.0 / sigma};
		// L = [l11 0; l21 l22] with L L^T = covariance; the residual is L^-1 r.
		const std::array<double, 3>& cov = *covariance;
		const double l11 = std::sqrt(cov[0]);
		const double l21 = cov[1] / l11;
		const double l22 = std::sqrt(cov[2] - l21 * l21);
		if (!(l11 > 0) || !(l22 > 0))
			return std::nullopt;
		return Whitening{1.0 / l11, -l21 / (l11 * l22), 1.0 / l22};
	}

	// A point in the reference frame seen by a camera: the pixel camera::project<T> predicts, against
	// the pixel observed, whitened. False when the point does not project, which fails the solve.
	template <typename T>
	static bool observe(const CameraModel& model, const T* const camera, const T* const inReference, const math::Vec2d& pixel,
						const Whitening& w, T* residual)
	{
		T inCamera[3];
		::ceres::AngleAxisRotatePoint(camera, inReference, inCamera);
		for (int i = 0; i < 3; ++i)
			inCamera[i] += camera[3 + i];
		const Projection<T> predicted = project(model, inCamera[0], inCamera[1], inCamera[2]);
		if (!predicted.ok())
			return false;
		const T du = predicted.u - T(pixel.x);
		const T dv = predicted.v - T(pixel.y);
		residual[0] = T(w.a) * du;
		residual[1] = T(w.b) * du + T(w.c) * dv;
		return true;
	}

	// One corner seen by one camera, posed by its body and then the camera.
	struct CornerResidual
	{
		const CameraModel* model;
		math::Vec3d point;
		math::Vec2d pixel;
		Whitening whitening;

		template <typename T>
		bool operator()(const T* const camera, const T* const body, T* residual) const
		{
			const T corner[3] = {T(point.x), T(point.y), T(point.z)};
			T inReference[3];
			::ceres::AngleAxisRotatePoint(body, corner, inReference);
			for (int i = 0; i < 3; ++i)
				inReference[i] += body[3 + i];
			return observe(*model, camera, inReference, pixel, whitening, residual);
		}
	};

	// One landmark seen by one camera: the landmark is its own parameter block, already in the
	// reference frame.
	struct PointResidual
	{
		const CameraModel* model;
		math::Vec2d pixel;
		Whitening whitening;

		template <typename T>
		bool operator()(const T* const camera, const T* const point, T* residual) const
		{
			return observe(*model, camera, point, pixel, whitening, residual);
		}
	};

	static ::ceres::LossFunction* lossOf(const registration::RobustLoss& loss)
	{
		switch (loss.family)
		{
			case registration::LossFamily::None:
				return nullptr;
			case registration::LossFamily::Huber:
				return new ::ceres::HuberLoss(loss.scale);
			case registration::LossFamily::Cauchy:
				return new ::ceres::CauchyLoss(loss.scale);
		}
		return nullptr;
	}

	static registration::Solution failure(registration::RefinementStatus status, std::string detail)
	{
		registration::Solution out;
		out.status = status;
		out.detail = std::move(detail);
		return out;
	}

	// --- CeresRefiner ---------------------------------------------------------------------

	Provenance CeresRefiner::provenance() const
	{
		return {"ceres", CERES_VERSION_STRING};
	}

	registration::Solution CeresRefiner::refine(const registration::Problem& problem) const
	{
		const core::Time start = core::Time::now();
		const std::size_t cameraCount = problem.models.size();
		if (problem.cameraFromReference.size() != cameraCount || problem.reference >= cameraCount)
			return failure(registration::RefinementStatus::Failed, "the problem's cameras do not match its models");
		if (problem.noise.pixelSigma <= 0)
			return failure(registration::RefinementStatus::Failed, "the noise model's pixel sigma is not positive");

		if (problem.observations.empty() && problem.landmarkObservations.empty())
			return failure(registration::RefinementStatus::Failed, "the problem has no observations");

		try
		{
			std::vector<Pose> cameras, bodies;
			for (const math::RigidTransformd& t : problem.cameraFromReference)
				cameras.push_back(poseOf(t));
			for (const math::RigidTransformd& t : problem.referenceFromBody)
				bodies.push_back(poseOf(t));
			std::vector<std::array<double, 3>> points;
			for (const math::Vec3d& p : problem.landmarks)
				points.push_back({p.x, p.y, p.z});

			::ceres::Problem::Options problemOptions;
			// One loss object for every residual, owned here rather than by the problem.
			problemOptions.loss_function_ownership = ::ceres::DO_NOT_TAKE_OWNERSHIP;
			const std::unique_ptr<::ceres::LossFunction> loss(lossOf(problem.loss));
			::ceres::Problem solver(problemOptions);

			const double sigma = problem.noise.pixelSigma;
			std::vector<bool> usedCamera(cameraCount, false), usedBody(bodies.size(), false), usedPoint(points.size(), false);
			for (const registration::RefinementObservation& o : problem.observations)
			{
				if (o.camera >= cameraCount || o.body >= bodies.size())
					return failure(registration::RefinementStatus::Failed, "an observation names no camera or body");
				const std::optional<Whitening> w = whitening(o.covariance, sigma);
				if (!w)
					return failure(registration::RefinementStatus::Failed, "a corner's covariance is not positive definite");
				solver.AddResidualBlock(new ::ceres::AutoDiffCostFunction<CornerResidual, 2, 6, 6>(
											new CornerResidual{&problem.models[o.camera], o.point, o.pixel, *w}),
										loss.get(), cameras[o.camera].data(), bodies[o.body].data());
				usedCamera[o.camera] = true;
				usedBody[o.body] = true;
			}
			for (const registration::LandmarkObservation& o : problem.landmarkObservations)
			{
				if (o.camera >= cameraCount || o.landmark >= points.size())
					return failure(registration::RefinementStatus::Failed, "a landmark observation names no camera or landmark");
				const std::optional<Whitening> w = whitening(o.covariance, sigma);
				if (!w)
					return failure(registration::RefinementStatus::Failed, "a landmark observation's covariance is not positive definite");
				solver.AddResidualBlock(
					new ::ceres::AutoDiffCostFunction<PointResidual, 2, 6, 3>(new PointResidual{&problem.models[o.camera], o.pixel, *w}),
					loss.get(), cameras[o.camera].data(), points[o.landmark].data());
				usedCamera[o.camera] = true;
				usedPoint[o.landmark] = true;
			}
			for (std::size_t c = 0; c < cameraCount; ++c)
			{
				if (usedCamera[c] && (!problem.freeCameras || c == problem.reference))
					solver.SetParameterBlockConstant(cameras[c].data());
			}

			::ceres::Solver::Options options;
			options.max_num_iterations = int(problem.maximumIterations);
			options.num_threads = 1;
			options.logging_type = ::ceres::SILENT;
			options.minimizer_progress_to_stdout = false;
			// The linear solver follows the problem's shape (ADR-0017).
			const std::size_t latent = std::size_t(std::count(usedBody.begin(), usedBody.end(), true)) +
									   std::size_t(std::count(usedPoint.begin(), usedPoint.end(), true));
			if (problem.freeCameras)
			{
				// The bodies and landmarks are eliminated first: a camera sees hundreds of them, each a
				// few cameras.
				options.linear_solver_type = ::ceres::SPARSE_SCHUR;
				options.sparse_linear_algebra_library_type = ::ceres::EIGEN_SPARSE;
				auto ordering = std::make_shared<::ceres::ParameterBlockOrdering>();
				for (std::size_t b = 0; b < bodies.size(); ++b)
				{
					if (usedBody[b])
						ordering->AddElementToGroup(bodies[b].data(), 0);
				}
				for (std::size_t p = 0; p < points.size(); ++p)
				{
					if (usedPoint[p])
						ordering->AddElementToGroup(points[p].data(), 0);
				}
				for (std::size_t c = 0; c < cameraCount; ++c)
				{
					if (usedCamera[c])
						ordering->AddElementToGroup(cameras[c].data(), 1);
				}
				options.linear_solver_ordering = ordering;
			}
			else if (latent <= 1)
				options.linear_solver_type = ::ceres::DENSE_QR; // one board pose or one point: tiny
			else
			{
				// Every camera held: the unknowns are independent of one another, so the normal
				// equations are block diagonal and sparse Cholesky factors them in linear time, where
				// dense QR of thousands of landmarks would not fit in memory.
				options.linear_solver_type = ::ceres::SPARSE_NORMAL_CHOLESKY;
				options.sparse_linear_algebra_library_type = ::ceres::EIGEN_SPARSE;
			}
			std::string invalid;
			if (!options.IsValid(&invalid))
				return failure(registration::RefinementStatus::Failed, "Ceres refused the solver options: " + invalid);

			::ceres::Solver::Summary summary;
			::ceres::Solve(options, &solver, &summary);

			registration::Solution out;
			// Which linear solver ran is part of the account: the rule above is otherwise visible only
			// as time, and Ceres may substitute one (a Schur solver with nothing to eliminate).
			out.detail = summary.BriefReport() + ", linear solver " + ::ceres::LinearSolverTypeToString(summary.linear_solver_type_used);
			out.iterations = std::uint32_t(summary.iterations.size());
			out.initialCost = summary.initial_cost;
			out.finalCost = summary.final_cost;
			switch (summary.termination_type)
			{
				case ::ceres::CONVERGENCE:
					out.status = registration::RefinementStatus::Converged;
					break;
				case ::ceres::NO_CONVERGENCE:
					out.status = registration::RefinementStatus::IterationLimit;
					break;
				default:
					out.status = registration::RefinementStatus::Failed;
					break;
			}
			if (out.usable())
			{
				for (const Pose& p : cameras)
					out.cameraFromReference.push_back(transformOf(p));
				for (const Pose& p : bodies)
					out.referenceFromBody.push_back(transformOf(p));
				for (const std::array<double, 3>& p : points)
					out.landmarks.push_back(math::Vec3d{p[0], p[1], p[2]});
			}
			out.elapsed = core::Time::now() - start;
			return out;
		}
		catch (const std::bad_alloc&)
		{
			return failure(registration::RefinementStatus::ResourceExhausted,
						   "the machine ran out of memory for a problem of " + std::to_string(problem.observations.size()) +
							   " corners and " + std::to_string(problem.landmarkObservations.size()) + " landmark observations");
		}
	}
} // namespace lain::camera::ceres
