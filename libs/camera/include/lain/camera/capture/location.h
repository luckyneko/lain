#pragma once

#include "lain/camera/capture/capturegroup.h"

#include <lain/core/uri.h>
#include <lain/media/framesequence.h>

#include <string>
#include <string_view>
#include <vector>

// Where footage lies, spelled the way identities need it (2026-10-21). A camera's identity, and a
// frame's source wherever it enters an identity, are spelled relative to the folder the footage lies
// in, so the same footage gives the same identities, and with them the same held-out evidence and the
// same resamples, wherever it sits on disk. Before this a camera was named by its footage's absolute
// source, and the same rig in another folder held out other evidence (WORK.md, M9's 2026-10-20 review).
// A stand-in until a capture manifest assigns durable identities (CONTEXT.md, "Capture manifest").
//
// A uri is read as '/'- (or '\'-) separated components, whatever its scheme: a location is a prefix of
// components, never a filesystem lookup.
namespace lain::camera::capture
{
	// The folder `footage` lies in. Each camera's LOCATION is the one source all its frames come from (a
	// video file), or else the deepest folder holding every one of its frames' sources (a folder of
	// stills). The root is the deepest folder holding every camera's location strictly within it, so
	// each camera keeps a name under it: one camera's root is the folder its location is in. Empty when
	// no camera has a frame.
	std::string footageRoot(const std::vector<media::FrameSequence>& footage);

	// `source` spelled relative to `root`, its components joined by '/'; spelled whole when `root` is
	// empty or `source` does not lie strictly within it.
	std::string relativeTo(const core::Uri& source, std::string_view root);

	// Each camera named by its location relative to the footage's root: "cam0" for a folder of stills,
	// "cam0.mp4" for a video. Two cameras sharing one location, such as two numbered patterns in one
	// folder, are named by their first frame's source relative to the root instead. Empty footage names
	// nobody, so its identity is empty.
	std::vector<CameraIdentity> identitiesByLocation(const std::vector<media::FrameSequence>& footage);
} // namespace lain::camera::capture
