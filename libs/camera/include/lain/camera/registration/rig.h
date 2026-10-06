#pragma once

#include "lain/camera/cameramodel.h"
#include "lain/camera/capture/capturegroup.h"

#include <lain/media/framesequence.h>

// The cameras of a registration dataset, whichever method registers them: an identity and a
// calibrated model held fixed, and the footage its capture-group members name.
namespace lain::camera::registration
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
} // namespace lain::camera::registration
