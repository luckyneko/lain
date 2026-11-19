#pragma once

#include "selection.h"

#include <lain/camera/cameramodel.h>
#include <lain/image/color.h>
#include <lain/math/types.h>
#include <lain/render/viewport.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>

#include <vector>

namespace rigplan
{
	// What the Viewport draws over a rig plan's scene that no capture ever records: each camera's frustum
	// and body, each light's marker and a spot's cone, each fixed point's marker, each mover's path and
	// keys, and whatever is selected in a highlight colour. All of it is render::Segments, from the plan
	// as it was meant (nominalLayout), so it is drawn where the gizmo moves it.

	// How big a marker is drawn round its point, and so how big a target it is to click (picking.h): a
	// light's, a key's, a fixed point's.
	inline constexpr double kMarkerRadius = 0.04; // metres

	// Samples along each side of a camera's image border, so a distorted lens's frustum bends as its
	// field of view does.
	inline constexpr int kFrustumSamples = 16;

	// The colours each is drawn in, linear light.
	inline const lain::image::ColorRGBf kCameraColour{0.25f, 0.6f, 0.95f};
	inline const lain::image::ColorRGBf kLightColour{1.0f, 0.8f, 0.25f};
	inline const lain::image::ColorRGBf kPathColour{0.85f, 0.35f, 0.85f};
	inline const lain::image::ColorRGBf kMountColour{0.7f, 0.7f, 0.7f};
	inline const lain::image::ColorRGBf kSelectedColour{1.0f, 0.45f, 0.05f};

	struct OverlayOptions
	{
		double frustumDepth = 1.0; // metres along each of a camera's rays its frustum reaches, and a spot's cone
		Selection selection;
	};

	// A camera's image border unprojected through `model` at `depth` metres along each ray, in the camera
	// frame: kFrustumSamples points a side, from the image's top-left outer corner (-0.5, -0.5) round
	// clockwise as the image is seen, so the corners are points 0, n, 2n and 3n. A border pixel the model
	// cannot unproject is left out.
	std::vector<lain::math::Vec3d> frustumOutline(const lain::camera::CameraModel& model, double depth);

	// The segments over `plan`, laid out as `layout` has it, at the frame `scene` was built at. The
	// selection's own segments, and the outline of every instance of `scene` it owns, are drawn in
	// kSelectedColour: a mount's are its pole's and the cameras and lights on it, a specification's every
	// camera or light it describes.
	std::vector<lain::render::Segment> overlays(const lain::rigplan::RigPlan& plan, const lain::rigplan::NominalLayout& layout,
												const lain::rigplan::SceneResult& scene, const OverlayOptions& options);
} // namespace rigplan
