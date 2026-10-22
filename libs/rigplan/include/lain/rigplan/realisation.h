#pragma once

#include "lain/rigplan/rigplan.h"

#include <lain/camera/cameramodel.h>
#include <lain/core/uuid.h>
#include <lain/math/rigidtransform.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lain::rigplan
{
	// One draw (CONTEXT.md, "Realisation"): a unit deviation in [-1, 1), the same on every platform,
	// chosen by the rig plan's seed, the stable identity of what the parameter belongs to and the
	// parameter's document key — never by list order or a display name, so an edit to one camera
	// re-rolls no other. It is SHA-256 over a versioned canonical text of the three, its first 53 bits
	// read as a fraction. A realisation multiplies it by its category's scale and the tolerance.
	double unitDeviation(std::uint64_t seed, const core::Uuid& owner, std::string_view key);

	// A mount where this build has it.
	struct RealisedMount
	{
		core::Uuid id;
		math::RigidTransformd planFromMount; // the mount's frame (plan-frame axes, turned by its yaw)
	};

	// A camera as this build has it: what its capture renders use and its ground truth records.
	struct RealisedCamera
	{
		core::Uuid id;
		std::string name;
		camera::CameraModel model;
		// Camera-frame coordinates (X right, Y down, Z forward) into the plan frame.
		math::RigidTransformd planFromCamera;
	};

	// A light as this build has it. Lights have no tolerances yet, so it is the plan's.
	struct RealisedLight
	{
		core::Uuid id;
		math::RigidTransformd planFromLight; // camera-frame axes: Z is the direction it shines
	};

	// One physical build of a rig plan (CONTEXT.md, "Realisation"), in the order the rig plan lists
	// its mounts, cameras and lights.
	struct Realisation
	{
		std::vector<RealisedMount> mounts;
		std::vector<RealisedCamera> cameras;
		std::vector<RealisedLight> lights;
	};

	enum class RealisationProblem
	{
		InvalidPlan,		  // the rig plan has a problem (check()); the detail is its diagnostic
		InvalidRealisedModel, // a camera's draws make no camera model
	};

	struct RealisationDiagnostic
	{
		RealisationProblem problem;
		std::string detail; // which camera and why, for a person
	};

	// The outcome of realise(): a realisation, or every reason there is none.
	struct RealisationResult
	{
		std::optional<Realisation> realisation;
		std::vector<RealisationDiagnostic> diagnostics; // empty exactly when `realisation` is set
	};

	// Draw every tolerance in `plan` once, under its scales. At scales of 0 this is the nominal rig
	// plan exactly: every camera's model is its specification's nominal model, every pose the planned
	// one. A camera whose draws make no camera model refuses the whole realisation, naming the camera,
	// and is never drawn again: a quiet redraw would bias the build toward the valid part of the band.
	//
	// Per camera, with s the intrinsic scale and u its draw for each key:
	//   * focalLength: fx and fy times (1 + s u tolerance);
	//   * principalPoint.x / .y: cx / cy plus s u (tolerance / pitch) pixels;
	//   * distortion: every coefficient times (1 + s u tolerance).
	// Per mount and placement, under the installation scale: position.x / .y (and .z for a fixed
	// point) and yaw on a mount; offset.x / .y / .z, pan, tilt and roll on a placement.
	RealisationResult realise(const RigPlan& plan);

	// A placed camera's or light's pose in its mount's frame: its mounting point at `offset`, aimed by
	// `aim` (see Aim), camera-frame axes. Exposed so a test can hold the aim convention to a hand-placed
	// camera, and the viewport can draw a plan's nominal poses without a realisation.
	math::RigidTransformd mountFromPlacement(const math::Vec3d& offset, const Aim& aim);
} // namespace lain::rigplan
