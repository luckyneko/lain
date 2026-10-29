#pragma once

#include "lain/camera/cameramodel.h"
#include "lain/camera/capture/capturegroup.h"

#include <lain/media/framesequence.h>

// The cameras of a fixed rig: an identity, a calibrated model held fixed, and the footage its
// capture-group members name. Below both of their consumers, so feature extraction and registration
// share one type without either naming the other.
namespace lain::camera
{
	// One camera of a rig: its identity and its calibrated model, held fixed.
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
} // namespace lain::camera
