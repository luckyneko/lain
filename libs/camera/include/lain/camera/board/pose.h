#pragma once

#include "lain/camera/board/observation.h"
#include "lain/camera/board/specification.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/provenance.h"

#include <lain/core/factory.h>
#include <lain/math/rigidtransform.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A board's pose in ONE view under a camera model held fixed (CONTEXT.md, "Pose solver"). Owned
// by the board module because both calibration (validating a model on held-out views) and
// registration (placing cameras from shared views of a board) need it, and neither may own it
// (ADR-0016, amended 2026-10-05).
namespace lain::camera::board
{
	enum class PoseStatus
	{
		Solved,		   // at least one pose
		TooFewCorners, // fewer than four corners the pattern has and the model can unproject
		NoSolution,	   // the backend found no pose
		NoBackend,	   // this build has no pose solver
	};

	// One pose a backend proposed, cameraFromBoard, with how well it explains the view, measured
	// by lain's own projection rather than taken from the backend.
	struct PoseCandidate
	{
		math::RigidTransformd cameraFromBoard;
		double rmsAngle = 0;  // radians, between each corner's observed ray and the ray to the posed corner
		double rmsPixels = 0; // raw, for diagnosis only
	};

	// The outcome of solving one view: always a value, never a bare optional.
	//
	// A planar target can fit two poses almost equally well, when it is far away or nearly
	// square-on to the camera, and one view cannot tell them apart. So a backend that finds a
	// second solution returns it as `alternative`, and the choice belongs to a caller with more
	// evidence: registration chooses by what the other cameras saw. The order is the backend's
	// ranking, which on a noisy view can be the wrong way round; both are measured. A second pose the
	// backend reached that is the first one again (within a milliradian) is not reported.
	struct PoseResult
	{
		PoseStatus status = PoseStatus::NoSolution;
		std::string detail; // why there is no pose, when there is none
		std::optional<PoseCandidate> pose;
		std::optional<PoseCandidate> alternative;
		Provenance provenance;

		bool ok() const { return status == PoseStatus::Solved; }
	};

	// A pose solver backend (ADR-0004's service shape). It proposes poses; the facade measures them.
	class PoseSolver
	{
	public:
		virtual ~PoseSolver() = default;

		virtual Provenance provenance() const = 0;

		// The poses that fit `view` of `board` under `model`, best first: none, one, or two for a
		// planar target's two solutions. Called only with at least four usable corners.
		virtual std::vector<math::RigidTransformd> solve(const CameraModel& model, const Specification& board,
														 const Observation& view) const = 0;
	};

	// The process-wide pose solver registry, keyed by backend name ("opencv").
	core::Factory<PoseSolver>& poseSolverRegistry();

	// Whether this build can solve board poses at all: whether any backend registered.
	bool canSolvePose();

	// The pose of `board` in `view` with `model` held fixed, through the registered backend. With
	// none registered the result is NoBackend, so a caller learns about a missing capability from
	// the same value it reads every other outcome from.
	PoseResult pose(const CameraModel& model, const Specification& board, const Observation& view);

	// How well a posed board explains a view: each corner's observed ray against the ray to the
	// corner at `cameraFromBoard`, through lain's projection. `corners` counts the corners measured
	// (a corner the model cannot project or unproject is skipped).
	struct ViewResidual
	{
		std::uint32_t corners = 0;
		double sumSquaredAngle = 0;	 // radians squared
		double sumSquaredPixels = 0; // pixels squared
		double worstPixels = 0;
	};
	ViewResidual measure(const CameraModel& model, const Specification& board, const Observation& view,
						 const math::RigidTransformd& cameraFromBoard);
} // namespace lain::camera::board
