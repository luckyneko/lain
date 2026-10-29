#pragma once

#include "lain/camera/cameramodel.h"
#include "lain/camera/feature/geometry.h"
#include "lain/camera/feature/tracks.h"
#include "lain/camera/observationgraph.h"
#include "lain/camera/provenance.h"

#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The incremental reconstruction a targetless method starts from: which views a seed pair can place,
// the seed, every view placed from it by absolute pose, and a start a refinement can take. Private to
// lain::camera: `src/` is never on a consumer's include path.
//
// A view is what a track's observation belongs to (feature::View). In targetless registration a view
// is a camera of the rig; in targetless calibration it will be one sampled frame of the moving camera.
// So the module holds a model per view and no identity: what a view is called, and what is reported
// about it, is its method's.
//
// Shared by targetless registration and targetless calibration, which may not depend on each other
// (ADR-0016). So this header, and the module behind it, include nothing from `registration/` or
// `calibration/`.
namespace lain::camera::reconstruction
{
	// The geometry facades' own minima (feature/geometry.h): five pairs pose two views, four point-rays
	// one. A seed pair needs this many shared tracks, and a view this many landmarks to be placed, or to
	// be kept once the landmarks behind it are left out.
	inline constexpr std::uint32_t kSeedShared = 5;
	inline constexpr std::uint32_t kPlaceLandmarks = 4;
	// How many view pairs are posed as seed candidates: of those whose placement reaches every required
	// view, the ones sharing the most tracks.
	inline constexpr std::size_t kSeedCandidates = 16;
	// The median triangulation angle a seed should clear, radians (2 degrees): a landmark the seed
	// triangulates is as uncertain in depth as its rays' noise over its parallax. Provisional until
	// sub-slice 9 measures real rigs; it ranks pairs and never refuses a rig.
	inline constexpr double kSeedAngle = 0.03490658503988659;

	// One usable observation of a track: its view, where it saw the landmark, and the ray.
	struct Seen
	{
		std::uint32_t view = 0;
		math::Vec2d pixel{0.0};
		std::optional<std::array<double, 3>> covariance;
		math::Vec3d ray{0.0}; // unit, in the view's frame
	};

	// A track with two or more usable observations, ascending by view.
	struct Usable
	{
		const feature::Track* track = nullptr;
		std::vector<Seen> seen;
	};

	// The usable evidence: tracks in the method's order, over a model per view.
	struct Evidence
	{
		std::vector<CameraModel> models; // per view
		std::vector<Usable> tracks;
	};

	// What one reconstruction fits: indices into Evidence::tracks. A bootstrap resample may name a track
	// twice; each entry is a landmark of its own, as each entry of a board resample is a body of its own.
	using Units = std::vector<std::uint32_t>;

	// The views of each unit, for the observation graph and placeability.
	std::vector<std::vector<std::uint32_t>> viewsOf(const Evidence& evidence, const Units& units);

	// Per view, the units that saw it.
	std::vector<std::vector<std::uint32_t>> unitsOf(std::size_t views, const std::vector<std::vector<std::uint32_t>>& units);

	// --- placeability --------------------------------------------------------------

	// Which views a seed pair can place (CONTEXT.md, "Placeable camera"): from the pair, repeatedly the
	// unplaced view in the most units having two or more placed views, needing kPlaceLandmarks of them,
	// ties to the lower index. Placing a view only adds to every other view's count, so the set reached
	// is the least fixed point whatever order views are added in; it depends on the seed.
	struct Closure
	{
		std::uint32_t a = 0, b = 0;
		std::vector<bool> placed;
		std::vector<std::uint32_t> rank;	// per placed view: its place in the order
		std::vector<std::uint32_t> support; // per placed view but the seeds: the units it was placed with
		std::uint32_t shared = 0;			// the units the seed pair shares

		bool covers(const std::vector<std::uint32_t>& views) const;
	};

	Closure closure(const std::vector<std::vector<std::uint32_t>>& units, const std::vector<std::vector<std::uint32_t>>& ofView,
					std::uint32_t a, std::uint32_t b);

	// The seed candidates over `units` for placing `required`: each pair of required views sharing at
	// least kSeedShared units, by how many they share, ties to (a, b). `covering` holds the closures that
	// reach every required view, at most `limit`, in that order; `top` is the first candidate's,
	// covering or not. A pair inside a closure that fell short cannot reach further (its own closure
	// lies inside that one), so it is skipped unexamined.
	struct Candidates
	{
		std::vector<Closure> covering;
		std::optional<Closure> top;
	};

	Candidates candidates(const ObservationGraph& graph, const std::vector<std::vector<std::uint32_t>>& units,
						  const std::vector<std::vector<std::uint32_t>>& ofView, const std::vector<std::uint32_t>& required,
						  std::size_t limit);

	// The seed candidates for a method that places what it can, rather than needing every view placed
	// (targetless calibration): each of `pairs` sharing at least kSeedShared units, by how many they
	// share, ties to (a, b); of their closures, those that reach the most views, at most `limit`, in
	// that order. Empty when no pair shares enough. A pair inside a closure that reaches fewer views
	// than the widest found so far cannot reach as far (its own closure lies inside that one), so it
	// is skipped unexamined. `pairs` are the edges of an observation graph over `units`, or those of
	// them the method lets seed.
	std::vector<Closure> widest(const std::vector<GraphEdge>& pairs, const std::vector<std::vector<std::uint32_t>>& units,
								const std::vector<std::vector<std::uint32_t>>& ofView, std::size_t limit);

	// --- initialisation ------------------------------------------------------------

	struct Seed
	{
		std::uint32_t a = 0, b = 0;
		math::RigidTransformd bFromA; // a unit baseline
		std::uint32_t inliers = 0;
		double angle = 0; // the median triangulation angle of its inliers that triangulate, radians
		// Its inliers whose rays meet at less than the minimum parallax, so that they triangulate to no
		// depth: points at infinity. A method asks this, not `angle`, whether a pair has any parallax at
		// all: when most inliers are at infinity, the few that triangulate are what decides `angle`, and
		// a slightly wrong model's rays meet at a spurious angle where a true model's are parallel.
		std::uint32_t atInfinity = 0;
		bool belowFloor = false;
	};

	// The seed: each candidate's relative pose from the tracks its two views share, then the one with
	// the most inliers whose median angle clears kSeedAngle, or the widest when none does. Unset when
	// no candidate could be posed. `geometry` receives the first pose's provenance, unless it already
	// names a backend.
	std::optional<Seed> chooseSeed(const Evidence& evidence, const Units& units, const std::vector<Closure>& covering,
								   const feature::GeometryRequest& request, Provenance& geometry);

	// Every view the seed can place, in the seed's first view's frame (the world) at a unit baseline:
	// landmarks triangulated from the placed views, then the unplaced view seeing the most of them
	// placed by absolute pose, until none more can be. A view it cannot place is left unset; whether
	// that fails is the method's to say.
	struct Placement
	{
		std::vector<std::optional<math::RigidTransformd>> viewFromWorld;
		std::vector<std::optional<math::Vec3d>> landmarks; // per unit entry, in the world
		std::vector<std::uint32_t> order;				   // the views, as placed
	};

	Placement place(const Evidence& evidence, const Units& units, const Seed& seed, const feature::GeometryRequest& request);

	// A placed reconstruction rebased on the reference view, with every observation its view cannot
	// project left out: a landmark behind a view at the start fails the whole refinement (ADR-0017).
	// What refinement starts from. Every view must have been placed.
	struct Start
	{
		std::vector<math::RigidTransformd> viewFromReference;
		std::vector<std::optional<math::Vec3d>> landmarks; // per unit entry, in the reference frame; unset: not refined
		std::vector<std::vector<bool>> kept;			   // per unit entry, per observation: refined
		std::uint32_t behind = 0;						   // observations left out
		std::vector<std::uint32_t> starved;				   // views left seeing fewer than kPlaceLandmarks landmarks
	};

	Start startFrom(const Evidence& evidence, const Units& units, const Placement& placement, std::uint32_t reference);
} // namespace lain::camera::reconstruction
