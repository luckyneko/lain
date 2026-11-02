// Looking through a camera (M15 slice 9d, lookthrough.h): which camera the view follows, its nominal or
// realised model and pose, the resolution steps it is captured at, and how a recording is shown.

#include "editorplan.h"
#include "lookthrough.h"
#include "selection.h"
#include "testcameras.h" // libs/render/test: one strongly distorted camera per distortion model

#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/image/color.h>
#include <lain/image/image.h>
#include <lain/image/imageview.h>
#include <lain/render/capture.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/sensor.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <variant>

namespace rp = lain::rigplan;
namespace image = lain::image;
using rigplan::LookChoice;
using rigplan::LookModel;
using rigplan::Selected;
using rigplan::Selection;

static rp::RigPlan plan()
{
	return editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
}

TEST_CASE("the view looks through the camera selected last, and a pin keeps it", "[rigplan][lookthrough]")
{
	rp::RigPlan p = plan();
	LookChoice choice;
	rigplan::follow(choice, p, Selection::object(Selected::Box, editorplan::boxId));
	CHECK_FALSE(choice.camera); // nothing to look through until a camera is selected

	rigplan::follow(choice, p, Selection::object(Selected::Camera, editorplan::cam1Id));
	CHECK(choice.camera == editorplan::cam1Id);
	rigplan::follow(choice, p, Selection::object(Selected::Board, editorplan::boardId));
	CHECK(choice.camera == editorplan::cam1Id); // selecting something else keeps it
	rigplan::follow(choice, p, Selection::object(Selected::Camera, editorplan::cam2Id));
	CHECK(choice.camera == editorplan::cam2Id);

	choice.pinned = true;
	rigplan::follow(choice, p, Selection::object(Selected::Camera, editorplan::cam1Id));
	CHECK(choice.camera == editorplan::cam2Id); // pinned, it stays

	// A camera the plan no longer has is let go of, and the pin with it.
	p.cameras.pop_back();
	rigplan::follow(choice, p, Selection::object(Selected::Camera, editorplan::cam1Id));
	CHECK_FALSE(choice.pinned);
	CHECK(choice.camera == editorplan::cam1Id);
	p.cameras.clear();
	rigplan::follow(choice, p, Selection::nothing());
	CHECK_FALSE(choice.camera);
}

TEST_CASE("Realised is the camera realise() builds, or its refusal; Nominal works on a plan with a problem", "[rigplan][lookthrough]")
{
	rp::RigPlan p = plan();
	p.scales.intrinsic = 1.0; // the installation follows, locked
	// So cam1's pose is drawn too, as well as its model.
	p.cameras.front().tolerance.offset = lain::core::Length::from<lain::core::Length::Millimetres>(5.0);
	p.cameras.front().tolerance.panDegrees = 0.5;
	REQUIRE(rp::check(p).empty());
	const rp::RealisationResult drawn = rp::realise(p);
	REQUIRE(drawn.realisation);
	const rp::RealisedCamera& truth = drawn.realisation->cameras.front();
	REQUIRE(truth.id == editorplan::cam1Id);

	const rigplan::LookThrough realised = rigplan::lookThrough(p, editorplan::cam1Id, LookModel::Realised);
	REQUIRE(realised.ready());
	CHECK(realised.name == "cam1");
	REQUIRE(realised.model);
	CHECK(rigplan::sameModel(*realised.model, truth.model));
	CHECK(realised.planFromCamera.translation() == truth.planFromCamera.translation());
	CHECK(realised.planFromCamera.rotation() == truth.planFromCamera.rotation());
	CHECK(std::holds_alternative<rp::Realisation>(realised.layout));
	CHECK(realised.recorder);
	CHECK(rigplan::captureView(realised).worldFromCamera.translation() == truth.planFromCamera.translation());

	// The nominal camera is another: the plan's draws moved the realised one.
	const rigplan::LookThrough nominal = rigplan::lookThrough(p, editorplan::cam1Id, LookModel::Nominal);
	REQUIRE(nominal.ready());
	REQUIRE(nominal.model);
	CHECK_FALSE(rigplan::sameModel(*nominal.model, *realised.model));
	CHECK(nominal.planFromCamera.translation() != realised.planFromCamera.translation());
	CHECK(std::holds_alternative<rp::NominalLayout>(nominal.layout));

	// cam1 on its pole's axis, inside the pole: realise() refuses the plan, and Realised says why rather
	// than fall back. Nominal still looks through it, and through cam2.
	p.cameras.front().offset = {0.0, 0.0, 1.5};
	const rp::RealisationResult refused = rp::realise(p);
	REQUIRE_FALSE(refused.realisation);
	const rigplan::LookThrough none = rigplan::lookThrough(p, editorplan::cam1Id, LookModel::Realised);
	CHECK_FALSE(none.ready());
	CHECK_FALSE(none.model);
	REQUIRE(none.problems.size() == refused.diagnostics.size());
	for (std::size_t i = 0; i < none.problems.size(); ++i)
		CHECK(none.problems[i] == refused.diagnostics[i].detail);
	CHECK(rigplan::lookThrough(p, editorplan::cam1Id, LookModel::Nominal).ready());
	CHECK(rigplan::lookThrough(p, editorplan::cam2Id, LookModel::Nominal).ready());

	// A camera that is not there cannot be looked through either way.
	CHECK_FALSE(rigplan::lookThrough(p, editorplan::boxId, LookModel::Nominal).ready());
}

TEST_CASE("two models are one camera only when every parameter is", "[rigplan][lookthrough]")
{
	// So a ray table is built again for a new lens or step, and never for an edit that leaves the model.
	for (int i = 0; i < lain::render::testing::kStrongModelCount; ++i)
	{
		INFO("model " << i);
		const lain::camera::CameraModelParameters p = lain::render::testing::strongCamera(i);
		const lain::camera::CameraModel model = *lain::camera::CameraModel::create(p).model;
		CHECK(rigplan::sameModel(model, *lain::camera::CameraModel::create(p).model));
		CHECK_FALSE(rigplan::sameModel(model, *rigplan::modelAtDivisor(model, 2).model));
		lain::camera::CameraModelParameters moved = p;
		moved.intrinsics.cy += 1e-9;
		CHECK_FALSE(rigplan::sameModel(model, *lain::camera::CameraModel::create(moved).model));
	}
	// A coefficient, and the model a coefficient set belongs to.
	lain::camera::CameraModelParameters bent = lain::render::testing::strongCamera(1);
	std::get<lain::camera::BrownConrady5>(bent.distortion).k2 += 1e-9;
	CHECK_FALSE(rigplan::sameModel(*lain::camera::CameraModel::create(lain::render::testing::strongCamera(1)).model,
								   *lain::camera::CameraModel::create(bent).model));
	lain::camera::CameraModelParameters other = lain::render::testing::strongCamera(1);
	const lain::camera::BrownConrady5 forward = std::get<lain::camera::BrownConrady5>(other.distortion);
	other.distortion = lain::camera::ModifiedBrownConrady5{forward.k1, forward.k2, forward.p1, forward.p2, forward.k3};
	CHECK_FALSE(rigplan::sameModel(*lain::camera::CameraModel::create(lain::render::testing::strongCamera(1)).model,
								   *lain::camera::CameraModel::create(other).model));
}

TEST_CASE("the scene looked through is the capture's: uncut, lit by the plan alone", "[rigplan][lookthrough]")
{
	const rp::RigPlan p = plan();
	rp::SceneAssets assets;
	rp::sceneGeometry(p, assets);
	const rigplan::LookThrough look = rigplan::lookThrough(p, editorplan::cam1Id, LookModel::Nominal);
	const rp::SceneResult scene = rigplan::lookThroughScene(p, look, assets, 3);
	const rp::SceneResult expected = rp::renderScene(p, std::get<rp::NominalLayout>(look.layout), assets, 3);
	CHECK(scene.scene.instances.size() == expected.scene.instances.size());
	CHECK(scene.scene.ambient.r == 0.0f);
	CHECK(scene.scene.lights.size() == p.lights.size());
}

TEST_CASE("the step is the largest of 1/1 to 1/8 that fits the pane", "[rigplan][lookthrough]")
{
	const lain::camera::ImageGeometry hd{1920, 1080};
	CHECK(rigplan::lookDivisor(hd, 2560, 1440) == 1);
	CHECK(rigplan::lookDivisor(hd, 1920, 1080) == 1);
	CHECK(rigplan::lookDivisor(hd, 1919, 1080) == 2);
	CHECK(rigplan::lookDivisor(hd, 960, 540) == 2);
	CHECK(rigplan::lookDivisor(hd, 600, 400) == 4);
	CHECK(rigplan::lookDivisor(hd, 240, 135) == 8);
	CHECK(rigplan::lookDivisor(hd, 10, 10) == 8); // even an eighth does not fit: an eighth, never less

	const lain::camera::CameraModel model = *lain::camera::CameraModel::create(lain::render::testing::strongCamera(1)).model;
	const lain::camera::ModelResult quarter = rigplan::modelAtDivisor(model, 4);
	REQUIRE(quarter.model);
	CHECK(quarter.model->image() == lain::camera::ImageGeometry{160, 120});
	CHECK(rigplan::settingsAtDivisor({}, 4).interpolationTolerance == 4 * lain::render::CaptureSettings{}.interpolationTolerance);
}

TEST_CASE("a camera whose full-size ray table builds builds at every step", "[rigplan][lookthrough]")
{
	// Through every distortion model, strongly: the step's tolerance is the capture's divided by the step,
	// since an interpolation error in pixels grows as one over the scale.
	const lain::render::CaptureSettings capture;
	for (int i = 0; i < lain::render::testing::kStrongModelCount; ++i)
	{
		const lain::camera::CameraModel model = *lain::camera::CameraModel::create(lain::render::testing::strongCamera(i)).model;
		const lain::render::RayTableResult full = lain::render::RayTable::build(model, capture);
		INFO("model " << i);
		REQUIRE(full.table);
		for (const std::uint32_t divisor : {2u, 4u, 8u})
		{
			INFO("1/" << divisor);
			const lain::camera::ModelResult at = rigplan::modelAtDivisor(model, divisor);
			REQUIRE(at.model);
			const lain::render::RayTableResult table = lain::render::RayTable::build(*at.model, rigplan::settingsAtDivisor(capture, divisor));
			CHECK(table.table);
		}
	}

	// The capture's own tolerance at an eighth refuses the strong rational camera, whose interpolation error
	// at full size is under a quarter of it: the step's tolerance is what keeps it.
	const lain::camera::CameraModel rational = *lain::camera::CameraModel::create(lain::render::testing::strongCamera(4)).model;
	CHECK_FALSE(lain::render::RayTable::build(*rigplan::modelAtDivisor(rational, 8).model, capture).table);
}

// A light of one grey on every pixel, linear, as a capture returns it.
static image::Image greyLight(float value)
{
	image::Image light(4, 2, image::PixelFormat::RGBA32F, image::ColorSpace::Linear);
	auto view = light.as<image::ColorRGBAf>();
	for (int y = 0; y < 2; ++y)
	{
		for (int x = 0; x < 4; ++x)
			view(x, y) = image::ColorRGBAf(value, value, value, 1.0f);
	}
	return light;
}

// lain::image's sRGB encoding of linear light, as an 8-bit code.
static int srgbCode(double linear)
{
	const double encoded = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
	return int(std::lround(encoded * 255.0));
}

TEST_CASE("the display decodes a recording by its own curve, and shows it in sRGB", "[rigplan][lookthrough]")
{
	// Whatever the curve, depth and channels a sensor records with, the picture shown is the light it
	// recorded, in sRGB: to within a code, since the ADC and the output each quantise.
	for (const image::ColorSpace curve : {image::ColorSpace::Linear, image::ColorSpace::sRGB, image::ColorSpace::BT709})
	{
		for (const std::uint32_t bits : {8u, 16u})
		{
			for (const rp::SensorChannels channels : {rp::SensorChannels::Rgb, rp::SensorChannels::Grey})
			{
				rp::SensorModel sensor;
				sensor.output.curve = curve;
				sensor.output.bits = bits;
				sensor.output.format = bits == 16 ? rp::OutputFormat::Tiff : rp::OutputFormat::Png;
				sensor.channels = channels;
				const std::optional<rp::Recorder> recorder = rp::Recorder::create(sensor);
				REQUIRE(recorder);
				for (const float value : {0.05f, 0.18f, 0.5f, 0.9f})
				{
					INFO("curve " << int(curve) << ", " << bits << " bits, channels " << int(channels) << ", light " << value);
					const image::Image shown = rigplan::displayed(*recorder, greyLight(value));
					REQUIRE(shown.valid());
					CHECK(shown.pixelFormat() == image::PixelFormat::RGBA8);
					CHECK(shown.colorSpace() == image::ColorSpace::sRGB);
					CHECK(shown.width() == 4);
					const auto pixel = shown.as<image::ColorRGBA8>()(2, 1);
					const int expected = srgbCode(double(value));
					CHECK(std::abs(int(pixel.r) - expected) <= 1);
					CHECK(std::abs(int(pixel.g) - expected) <= 1);
					CHECK(std::abs(int(pixel.b) - expected) <= 1);
					CHECK(pixel.a == 255);
				}
			}
		}
	}
	// What is not a capture's light shows nothing.
	CHECK_FALSE(rigplan::displayed(*rp::Recorder::create({}), image::Image{}).valid());
}
