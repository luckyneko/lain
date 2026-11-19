#pragma once

#include "lain/rigplan/realisation.h"
#include "lain/rigplan/rigplan.h"

#include <lain/camera/registration/report.h>
#include <lain/core/uuid.h>
#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lain::rigplan
{
	// A synthetic capture's ground truth (CONTEXT.md, "Ground truth"): what it was actually rendered with,
	// computed through lain's own projection from the realisation, never from the rig plan's nominal
	// values. No GPU: the truth is the same on every machine, even where the pixels are not.

	// Where one camera is in the plan frame.
	struct CameraPose
	{
		core::Uuid id;
		std::string name;
		math::RigidTransformd planFromCamera; // camera-frame axes (X right, Y down, Z forward)
	};

	// Every realised camera's pose, in the rig plan's order.
	std::vector<CameraPose> cameraPoses(const Realisation& realisation);

	// A board corner where one camera sees it: its id (camera::board::Pattern's, row by row from the top
	// left) and its true pixel through the camera's realised model.
	struct CornerTruth
	{
		std::uint32_t corner = 0;
		math::Vec2d pixel{0.0};
	};

	// The corners of one board IN VIEW of one camera: those that project inside its image through its
	// realised model while the camera stands on the board's printed side. Occlusion is NOT tested: a
	// corner behind a pole or a prop is still listed (WORK.md M15, *Slice 5 planned*), and a consumer that
	// matches detections to truth by corner id simply finds no detection of it.
	struct CameraCorners
	{
		core::Uuid camera;
		std::string name;
		std::vector<CornerTruth> inView; // ascending by corner; empty when none is
	};

	// One board at one frame: its pose, and its corners in view of each camera.
	struct BoardTruth
	{
		core::Uuid board;
		std::string name;
		std::size_t frame = 0;
		math::RigidTransformd planFromBoard; // camera::board's board frame into the plan frame
		std::vector<CameraCorners> cameras;	 // every camera, in the rig plan's order
	};

	// Every board of `plan` at capture frame `frame`, in the rig plan's order. `realisation` must be
	// `plan`'s and `plan` must pass check(): a board whose pattern makes no board is skipped.
	std::vector<BoardTruth> boardTruth(const RigPlan& plan, const Realisation& realisation, std::size_t frame);

	// The camera the truth's rig is relative to: the rig plan's reference, or its first camera when it
	// names none. Null when there is no camera.
	const RealisedCamera* referenceCamera(const RigPlan& plan, const Realisation& realisation);

	// `poses` as a registered rig (CONTEXT.md, "Registered rig") relative to the camera named
	// `reference`: every camera's transform from that one's, metric, each named by its name, which is
	// its folder in a capture and so the identity M9 gives it (capture/location.h). Cameras ascending by
	// identity, as a registration reports them. So a registration that chose another reference is scored
	// against the truth derived again for that reference, never against a rig rebased after the fact.
	// `reference` must be one of `poses`.
	camera::registration::RegisteredRig rigRelativeTo(const std::vector<CameraPose>& poses, const std::string& reference);

	// The realisation's rig relative to referenceCamera(): what truth/rig.json holds. `plan` must have a
	// camera.
	camera::registration::RegisteredRig truthRig(const RigPlan& plan, const Realisation& realisation);
} // namespace lain::rigplan
