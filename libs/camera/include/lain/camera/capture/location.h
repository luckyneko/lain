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
	// The folder `footage` lies in. Each camera's LOCATION is the one source all its frames come from, or
	// else the deepest folder holding every one of its frames' sources. A source is what its opener names
	// it (media::FrameRef::source): a video file, or a folder of stills, which io::image::open opens as
	// ONE source named by the folder however many stills it holds (a numbered pattern is likewise one
	// source, named by its pattern). So a folder of stills is located at itself and a video at its file,
	// whatever their lengths, and the folder rule serves footage composed of several sources, such as
	// takes concatenated. A still that was a source of its own would be located at its file, since nothing
	// in a uri tells one still from a folder of them; no opener makes one. The root is the deepest folder
	// holding every camera's location strictly within it, so each camera keeps a name under it: one
	// camera's root is the folder its location is in. Empty when no camera has a frame.
	std::string footageRoot(const std::vector<media::FrameSequence>& footage);

	// `source` spelled relative to `root`, its components joined by '/'; spelled whole when `root` is
	// empty or `source` does not lie strictly within it.
	std::string relativeTo(const core::Uri& source, std::string_view root);

	// Each camera named by its location relative to the footage's root: "cam0" for a folder of stills,
	// and "cam0" for a video cam0.mp4 too, since a CONTAINER's file is named less its extension
	// (media::FrameSource::isContainer). The repo owner's choice (2026-11-04): a camera is named the same
	// whatever it was captured to, so its model document is cam0.json and a rig document says cam0
	// either way. Only the medium can say a source is a container, so a folder of stills whose name has a
	// dot in it, or a numbered pattern, keeps its whole name. Cameras that would share a name are named by
	// their first frame's source relative to the root instead: two sharing one location, such as two
	// composed from sources in one folder, and a video beside another of its name (cam0.mp4 and cam0.mov)
	// or beside a folder of stills of its name, whose first frames spell their whole locations, extensions
	// and all, so no two are given one identity. Empty footage names nobody, so its identity is empty.
	std::vector<CameraIdentity> identitiesByLocation(const std::vector<media::FrameSequence>& footage);
} // namespace lain::camera::capture
