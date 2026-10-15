#pragma once

#include "lain/camera/feature/status.h"
#include "lain/camera/provenance.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Two-view and absolute pose from rays, and the fundamental matrix of two images from pixels (ADR-0016,
// "Producers propose; lain decides"). A geometry backend proposes candidates and nothing else; the
// facades measure every candidate (a pose by angular residual and cheirality, through lain's own
// triangulation; a fundamental matrix by Sampson distance), choose one, and work out its inliers. A
// backend's own idea of its inliers is never asked for. Rays come from lain's unprojection, so no
// camera model crosses the seam; pixels are for the one question asked before there is a model.
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

	// One correspondence between two images, before any camera model: where A saw it and where B saw
	// it, each in its own image's SOURCE pixels, (0, 0) the centre of the top-left pixel.
	struct PixelPair
	{
		math::Vec2d a{0.0};
		math::Vec2d b{0.0};
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

		// Candidates for the fundamental matrix F of two images, x_bᵀ F x_a = 0 with x = (u, v, 1) in
		// source pixels, each of rank two; their scale and sign mean nothing. `threshold` is the
		// inlier distance in source pixels. Called only with at least eight pixel pairs.
		virtual std::vector<math::Mat3d> fundamentals(const std::vector<PixelPair>& pairs, double threshold,
													  std::uint64_t seed) const = 0;
	};

	// The process-wide geometry registry, keyed by backend name ("opencv").
	core::Factory<GeometrySolver>& geometryRegistry();

	// Whether this build can solve two-view or absolute pose at all: whether any backend registered.
	bool canSolveGeometry();

	struct GeometryRequest
	{
		// An inlier's rays are within this of where the pose puts them, radians. COLMAP's 4 px at a
		// focal length of 1000 px. It is also the parallax below which a pair is a point at infinity.
		// On rendered footage at that focal length, 2 of 503 tracks it verified were wrong (M9 slice 3,
		// sub-slice 9), and the repo owner kept it (2026-10-09) before a node exposed it
		// (registerCamerasTargetless' inlierAngleMrad). Real footage is still owed.
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

	struct FundamentalRequest
	{
		// An inlier is within this Sampson distance of the matrix, in source pixels: COLMAP's two-view
		// default. A caller that searched its images at a coarser scale states it in those pixels and
		// scales it here.
		double threshold = 4.0;
		std::uint64_t seed = 0;
	};

	struct FundamentalResult
	{
		Status status = Status::NoSolution;
		std::string detail; // why there is no matrix, when there is none
		// x_bᵀ F x_a = 0, normalised: unit Frobenius norm, its largest-magnitude entry positive (the
		// first in row-major order, on a tie). GLM's indexing is matrix[column][row].
		std::optional<math::Mat3d> matrix;
		std::vector<std::uint32_t> inliers; // indices into the input, ascending
		double rmsDistance = 0;				// over the inliers' Sampson distances, source pixels
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

	// The fundamental matrix of two images from pixel correspondences, for two images of a camera that
	// has no model yet. Each candidate is measured: a pair is an inlier when its Sampson distance,
	// the first-order distance from the pair to the matrix's epipolar geometry in both images, is
	// within the request's threshold. The candidate with the most inliers is chosen, then the least
	// summed squared distance, then a canonical order over its normalised entries, whatever order the
	// backend gave them in. A fundamental matrix ignores lens distortion, so a strongly distorted
	// image's peripheral pairs explain less.
	//
	// TooFew below eight usable pairs; NoSolution when no candidate has eight inliers.
	FundamentalResult fundamental(const std::vector<PixelPair>& pairs, const FundamentalRequest& request = {});
} // namespace lain::camera::feature
