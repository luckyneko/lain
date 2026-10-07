#pragma once

#include "lain/camera/cameramodel.h" // ImageGeometry
#include "lain/camera/capture/capturegroup.h"

#include <lain/math/types.h>
#include <lain/media/frameref.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Accepted feature tracks (CONTEXT.md, "Feature track"): the evidence a targetless method consumes,
// compact and backend-neutral. A track names the frames its positions came from and never holds an
// image, so a registration can keep hundreds of thousands of them without keeping a frame.
namespace lain::camera::feature
{
	// The frames one set of feature positions came from (ADR-0016, "A track's observations belong to
	// views"): one per camera in fixed-camera registration, where a feature's position is its median
	// across the camera's sampled frames.
	struct View
	{
		capture::CameraIdentity camera;
		ImageGeometry image;				 // the source image its pixels are in
		std::vector<media::FrameRef> frames; // in capture order
	};

	// Where one view saw a track's landmark (CONTEXT.md, "Scene-feature observation").
	struct SceneObservation
	{
		std::uint32_t view = 0;	   // into TrackSet::views
		math::Vec2d pixel{0.0};	   // SOURCE-image pixels: (0, 0) is the centre of the top-left pixel
		std::uint32_t support = 0; // how many of the view's frames the feature was found in
		// The pixel covariance (xx, xy, yy), as board::FeatureObservation's. Extraction states the
		// localisation it asked for as a proportion of the feature's size, which is in source pixels:
		// a larger feature carries a larger one, and so does a camera processed at a coarser scale,
		// whose features are found larger.
		std::optional<std::array<double, 3>> covariance;
	};

	// One static landmark across the capture: where each view that saw it saw it.
	struct Track
	{
		// A digest of the observations' cameras and pixels (SHA-256, hex), stable across runs, input
		// orders and execution policies, as a capture group's is.
		std::string identity;
		std::vector<SceneObservation> observations; // ascending by view, one per view, at least two
	};

	// What feature-track extraction accepted: the frames it examined and the tracks they support.
	struct TrackSet
	{
		std::vector<capture::CaptureGroup> groups; // the capture groups sampled, in capture order
		std::vector<View> views;				   // ascending by camera identity: every camera, tracks or not
		std::vector<Track> tracks;				   // ascending by identity
	};
} // namespace lain::camera::feature
