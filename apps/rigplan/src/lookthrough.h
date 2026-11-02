#pragma once

#include "selection.h"

#include <lain/camera/cameramodel.h>
#include <lain/core/uuid.h>
#include <lain/image/image.h>
#include <lain/math/rigidtransform.h>
#include <lain/render/capture.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>
#include <lain/rigplan/sensor.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rigplan
{
	// Looking through a camera (WORK.md M15 slice 9): which camera the camera view shows, as which model,
	// and how its picture is made, decided without a device. The picture is what a synthetic capture of
	// that camera would hold at the frame shown, drawn through the same scene and view writeCapture uses,
	// at a reduced resolution, and shown as the sensor records it without its noise.

	// Which model a camera is looked through with: as it was meant, or as the plan's draws build it.
	enum class LookModel
	{
		Nominal,
		Realised,
	};

	// Which camera the view looks through, and how.
	struct LookChoice
	{
		std::optional<lain::core::Uuid> camera; // the camera selected last, or the one pinned
		bool pinned = false;
		LookModel model = LookModel::Nominal;
	};

	// Follows `selection`: a camera selected becomes the one looked through, unless the view is pinned,
	// and anything else selected leaves it where it was. A camera the plan no longer has is let go of,
	// and the pin with it.
	void follow(LookChoice& choice, const lain::rigplan::RigPlan& plan, const Selection& selection);

	// One camera as it is looked through: its name, its model at full size and its pose, how its sensor
	// records, and what places the rest of the plan around it (the nominal layout, or the realisation),
	// or why it cannot be looked through.
	struct LookThrough
	{
		std::string name;
		std::optional<lain::camera::CameraModel> model;
		lain::math::RigidTransformd planFromCamera; // camera-frame coordinates into the plan frame
		std::optional<lain::rigplan::Recorder> recorder;
		std::variant<lain::rigplan::NominalLayout, lain::rigplan::Realisation> layout;
		std::vector<std::string> problems; // empty exactly when it can be looked through

		bool ready() const { return problems.empty(); }
	};

	// The camera `camera` names in `plan`, as `model` has it. Nominal is the camera as nominalLayout()
	// places it, its specification's nominal model, so it works on a plan with a problem elsewhere.
	// Realised is the camera realise() builds, model and pose; a plan realise() refuses has no realised
	// camera, and its reasons are the problems, never a fall back to the nominal one, which would show a
	// picture the capture would not hold.
	LookThrough lookThrough(const lain::rigplan::RigPlan& plan, const lain::core::Uuid& camera, LookModel model);

	// The scene looked through at `frame`, as writeCapture renders it: everything where `look`'s layout
	// places it, uncut, lit by the plan's own lights alone (no preview ambient), since it shows what a
	// capture holds. So a plan with no lights reads black.
	lain::rigplan::SceneResult lookThroughScene(const lain::rigplan::RigPlan& plan, const LookThrough& look,
												const lain::rigplan::SceneAssets& assets, std::size_t frame);

	// The view a capture of it renders through: its pose, at a capture's depth range.
	lain::render::CaptureView captureView(const LookThrough& look);

	// The look-through's resolution, a step of 1/1, 1/2, 1/4 or 1/8 of the camera's image: the largest
	// that fits a pane `width` x `height` pixels, so dragging a dock builds the ray table again only when
	// a step is crossed. Returned as its divisor, 1, 2, 4 or 8; 8 when even an eighth does not fit.
	std::uint32_t lookDivisor(const lain::camera::ImageGeometry& image, std::uint32_t width, std::uint32_t height);

	// `model` at a step of 1/`divisor`: its image divided by it (at least a pixel a side), through
	// camera::scaled, so the same scene lands at the same fraction of the picture.
	lain::camera::ModelResult modelAtDivisor(const lain::camera::CameraModel& model, std::uint32_t divisor);

	// Whether `a` and `b` are one camera, parameter for parameter: its image, intrinsics, distortion model
	// and every coefficient, exactly. So a ray table built for one serves the other, and is built again
	// only when the model changes, not every time the plan does.
	bool sameModel(const lain::camera::CameraModel& a, const lain::camera::CameraModel& b);

	// A capture's settings at a step of 1/`divisor`: the interpolation tolerance divided by the step,
	// which is to say multiplied by `divisor`. An interpolation error measured in pixels grows as one over
	// the scale, since a pixel's span of rays does, so to first order this accepts exactly the cameras a
	// capture at `settings` accepts.
	lain::render::CaptureSettings settingsAtDivisor(const lain::render::CaptureSettings& settings, std::uint32_t divisor);

	// What the look-through shows of `light`, a capture's RGBA32F linear light: the sensor's recording of
	// it without noise (its gain, ADC and output curve), decoded by the recording's own curve and encoded
	// for display as opaque 8-bit sRGB RGBA, a Grey sensor's in grey. The decode is in float, so a 16-bit
	// recording is not quantised to 8 bits before its curve is undone. Invalid when `light` is not valid
	// RGBA32F linear light.
	lain::image::Image displayed(const lain::rigplan::Recorder& recorder, const lain::image::Image& light);
} // namespace rigplan
