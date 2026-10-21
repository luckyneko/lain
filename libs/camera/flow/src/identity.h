#pragma once

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/capture/location.h>
#include <lain/media/framesequence.h>

#include <vector>

namespace lain::camera::detail
{
	// Cameras named by where their footage lies relative to the folder it lies in
	// (capture::identitiesByLocation): "cam0" for a folder of stills, "cam0.mp4" for a video. A stand-in
	// until a capture manifest assigns identities; empty footage names nobody. One rule for the
	// registration nodes' rig and the calibration nodes' camera, so a camera is named the same way
	// wherever its footage is wired, and the same footage gets the same name, and so the same held-out
	// evidence, wherever it sits on disk.
	inline std::vector<capture::CameraIdentity> identitiesOf(const std::vector<media::FrameSequence>& footage)
	{
		return capture::identitiesByLocation(footage);
	}

	// One camera named as identitiesOf names it: by its location within the folder that holds it.
	inline capture::CameraIdentity identityOf(const media::FrameSequence& footage)
	{
		return identitiesOf({footage}).front();
	}
} // namespace lain::camera::detail
