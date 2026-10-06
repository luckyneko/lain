#pragma once

#include "lain/camera/feature/status.h"
#include "lain/camera/provenance.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Two-view and absolute pose from rays (ADR-0016, "Producers propose; lain decides"). A geometry
// backend proposes candidate poses and nothing else; the facades measure every candidate by angular
// residual and cheirality, through lain's own triangulation, choose one, and work out its inliers.
// A backend's own idea of its inliers is never asked for. Rays come from lain's unprojection, so no
// camera model crosses the seam.
namespace lain::camera::feature
{
	// One correspondence between two cameras: the ray to it in A and the ray to it in B, each in its
	// own camera's frame.
	struct RayPair
	{
		math::Vec3d a{0.0, 0.0, 1.0};
		math::Vec3d b{0.0, 0.0, 1.0};
	};

	// A point in the reference frame, and the ray to it seen by one camera, in that camera's frame.
	struct PointRay
	{
		math::Vec3d point{0.0};
		math::Vec3d ray{0.0, 0.0, 1.0};
	};

	// A geometry backend (ADR-0004's service shape). Both calls are handed unit rays and return
	// candidates only, in any order; `angle` (radians) is the inlier threshold a robust estimator
	// inside the backend should use, and `seed` makes its sampling repeatable.
	class GeometrySolver
	{
	public:
		virtual ~GeometrySolver() = default;

		virtual Provenance provenance() const = 0;

		// Candidates for bFromA: a point in A's frame is apply()'d into B's. The translation's length
		// means nothing. Called only with at least five pairs.
		virtual std::vector<math::RigidTransformd> relativePoses(const std::vector<RayPair>& pairs, double angle,
																 std::uint64_t seed) const = 0;

		// Candidates for cameraFromReference. Called only with at least four point-rays.
		virtual std::vector<math::RigidTransformd> absolutePoses(const std::vector<PointRay>& pointRays, double angle,
																 std::uint64_t seed) const = 0;
	};

	// The process-wide geometry registry, keyed by backend name ("opencv").
	core::Factory<GeometrySolver>& geometryRegistry();

	// Whether this build can solve two-view or absolute pose at all: whether any backend registered.
	bool canSolveGeometry();

	struct GeometryRequest
	{
		// An inlier's rays are within this of where the pose puts them, radians. COLMAP's 4 px at a
		// focal length of 1000 px; provisional until M9 slice 3 sub-slice 7 measures SIFT on rendered
		// scenes. It is also the parallax below which a pair is a point at infinity.
		double angle = 0.004;
		std::uint64_t seed = 0;
	};

	struct GeometryResult
	{
		Status status = Status::NoSolution;
		std::string detail; // why there is no pose, when there is none
		// relativePose: bFromA with a unit translation. absolutePose: cameraFromReference.
		std::optional<math::RigidTransformd> pose;
		std::vector<std::uint32_t> inliers; // indices into the input, ascending
		double rmsAngle = 0;				// over the inliers' rays, radians
		std::uint32_t candidates = 0;		// the backend's proposals
		Provenance provenance;
		core::Time elapsed;

		bool ok() const { return status == Status::Ok; }
	};

	// The relative pose of two cameras from rays to shared points. Each candidate is measured: a pair
	// is triangulated and is an inlier when both rays are within the request's angle of the point and
	// it lies in front of both cameras. A pair whose parallax is below the angle is a point at
	// infinity: an inlier when its two rays agree under the candidate's rotation. The candidate with
	// the most inliers is chosen, whatever order the backend gave them in.
	//
	// TooFew below five usable pairs; NoSolution when no candidate has five inliers.
	GeometryResult relativePose(const std::vector<RayPair>& pairs, const GeometryRequest& request = {});

	// A camera's pose from rays to known points. A point-ray is an inlier when the ray is within the
	// request's angle of the posed point, which a point behind the camera never is.
	//
	// TooFew below four usable point-rays; NoSolution when no candidate has four inliers.
	GeometryResult absolutePose(const std::vector<PointRay>& pointRays, const GeometryRequest& request = {});
} // namespace lain::camera::feature
