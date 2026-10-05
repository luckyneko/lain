#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/board/specification.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/registration/report.h"
#include "lain/camera/registration/request.h"

#include <lain/media/framesequence.h>

#include <vector>

// Fixed-camera board registration, the first registration method module (ADR-0016, ADR-0017).
// Backend-neutral: detection, board poses and the global refinement go through the registered
// backends; everything that decides what the result MEANS (which groups fit and which validate,
// which of a planar board's two poses is the right one, how the rig is first put together, what an
// outlier is, how stable the result is, and what it is fit for) is here, so it is the same whichever
// backends run. `register` is a keyword, hence `registerCameras`.
namespace lain::camera::registration::board
{
	// One camera of a registration dataset: its identity and its calibrated model, held fixed.
	struct RigCamera
	{
		capture::CameraIdentity camera;
		CameraModel model;
	};

	// One camera with the footage its capture-group members name.
	struct RigFootage
	{
		capture::CameraIdentity camera;
		CameraModel model;
		media::FrameSequence footage;
	};

	// The detections of one capture group, one per member, in the order of group.members().
	struct GroupObservations
	{
		capture::CaptureGroup group;
		std::vector<camera::board::DetectionReport> detections;
	};

	// Register cameras from footage of a known board: detect it in every capture-group member's frame,
	// then everything registerCameras(detections) does. Always a report, successful or failed.
	//
	// Every check that needs no frame comes first, so a request that cannot succeed decodes nothing.
	// Frames are decoded one per task and released, so memory is a frame per worker however many
	// groups there are. With ExecutionPolicy::DeterministicDebug the same work runs serially.
	Report registerCameras(const std::vector<RigFootage>& cameras, const std::vector<capture::CaptureGroup>& groups,
						   const camera::board::Specification& board, const Request& request);

	// The same from detections already made. In order:
	// 1. a board pose for every usable detection, with a planar board's second pose kept;
	// 2. every k-th usable capture group, in identity order, held out to validate on, unless holding
	//    it out would split the camera graph;
	// 3. the camera observation graph over the rest, which must be one component;
	// 4. the registration reference, requested or chosen;
	// 5. initialisation over a maximum spanning tree of the graph, each relative pose chosen from
	//    every candidate the shared groups propose by how well it transfers across them;
	// 6. the global refinement, then outliers named by camera and group;
	// 7. held-out groups predicted member by member (the transfer residual);
	// 8. seeded bootstrap resampling of the groups for stability;
	// 9. the verdict against the fitness profile.
	//
	// Independent of input order: cameras and groups are put in identity order first.
	Report registerCameras(const std::vector<RigCamera>& cameras, const std::vector<GroupObservations>& groups,
						   const camera::board::Specification& board, const Request& request);
} // namespace lain::camera::registration::board
