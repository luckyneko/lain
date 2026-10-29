#include "lain/camera/board/pose.h"

#include "lain/camera/projection.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace lain::camera::board
{
	// Four is the least that fixes a board pose, as DetectionRequest::minimumCorners says.
	static constexpr std::uint32_t kMinimumCorners = 4;
	// A second pose within this rotation of the first is the same solution reached twice, not the
	// plane's other pose. Measured with OpenCV: refining both IPPE solutions of a clean view lands
	// them 3e-5 rad apart, while a genuine pair differs by about twice the board's tilt from square-on.
	static constexpr double kSamePose = 1e-3;

	static double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const double w = std::abs(math::dot(a.rotation(), b.rotation()));
		return 2.0 * std::acos(std::min(1.0, w));
	}

	static PoseCandidate candidate(const CameraModel& model, const Specification& board, const Observation& view,
								   const math::RigidTransformd& cameraFromBoard)
	{
		const ViewResidual residual = measure(model, board, view, cameraFromBoard);
		PoseCandidate out;
		out.cameraFromBoard = cameraFromBoard;
		if (residual.corners > 0)
		{
			out.rmsAngle = std::sqrt(residual.sumSquaredAngle / residual.corners);
			out.rmsPixels = std::sqrt(residual.sumSquaredPixels / residual.corners);
		}
		return out;
	}

	core::Factory<PoseSolver>& poseSolverRegistry()
	{
		static core::Factory<PoseSolver> registry;
		return registry;
	}

	bool canSolvePose()
	{
		return !poseSolverRegistry().keys().empty();
	}

	ViewResidual measure(const CameraModel& model, const Specification& board, const Observation& view,
						 const math::RigidTransformd& cameraFromBoard)
	{
		ViewResidual out;
		for (const FeatureObservation& f : view.features)
		{
			const std::optional<math::Vec3d> corner = board.cornerPosition(f.id);
			if (!corner)
				continue;
			const math::Vec3d point = cameraFromBoard.apply(*corner);
			const Projection<double> predicted = project(model, point.x, point.y, point.z);
			const Unprojection<double> observed = unproject(model, f.pixel.x, f.pixel.y);
			if (!predicted.ok() || !observed.ok())
				continue;
			const math::Vec3d ray{observed.x, observed.y, observed.z};
			const math::Vec3d towards = math::normalize(point);
			const double angle = std::atan2(math::length(math::cross(ray, towards)), math::dot(ray, towards));
			const double pixels = math::length(math::Vec2d{predicted.u, predicted.v} - f.pixel);
			out.sumSquaredAngle += angle * angle;
			out.sumSquaredPixels += pixels * pixels;
			out.worstPixels = std::max(out.worstPixels, pixels);
			++out.corners;
		}
		return out;
	}

	PoseResult pose(const CameraModel& model, const Specification& board, const Observation& view)
	{
		PoseResult out;
		const std::vector<std::string> backends = poseSolverRegistry().keys();
		if (backends.empty())
		{
			out.status = PoseStatus::NoBackend;
			out.detail = "this build has no board pose solver (configure with -DLAIN_CAMERA_OPENCV=ON)";
			return out;
		}

		std::uint32_t usable = 0;
		for (const FeatureObservation& f : view.features)
		{
			if (board.cornerPosition(f.id) && unproject(model, f.pixel.x, f.pixel.y).ok())
				++usable;
		}
		if (usable < kMinimumCorners)
		{
			out.status = PoseStatus::TooFewCorners;
			out.detail = std::to_string(usable) + " usable corners; a pose needs " + std::to_string(kMinimumCorners);
			return out;
		}

		const std::unique_ptr<PoseSolver> solver = poseSolverRegistry().create(backends.front());
		out.provenance = solver->provenance();
		const std::vector<math::RigidTransformd> poses = solver->solve(model, board, view);
		if (poses.empty())
		{
			out.status = PoseStatus::NoSolution;
			out.detail = "the backend found no pose";
			return out;
		}
		out.status = PoseStatus::Solved;
		out.pose = candidate(model, board, view, poses[0]);
		if (poses.size() > 1 && rotationBetween(poses[0], poses[1]) > kSamePose)
			out.alternative = candidate(model, board, view, poses[1]);
		return out;
	}
} // namespace lain::camera::board
