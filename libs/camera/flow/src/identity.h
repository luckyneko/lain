#pragma once

#include <lain/camera/capture/capturegroup.h>
#include <lain/media/framesequence.h>

#include <string>

namespace lain::camera::detail
{
	// A camera named by its footage's canonical source uri, a stand-in until a capture manifest assigns
	// identities; empty footage names nobody. One rule for the registration nodes' rig and the
	// calibration nodes' camera, so a camera is named the same way wherever its footage is wired.
	inline capture::CameraIdentity identityOf(const media::FrameSequence& footage)
	{
		return capture::CameraIdentity{footage.size() > 0 ? footage.frame(0).source.toString() : std::string{}};
	}
} // namespace lain::camera::detail
