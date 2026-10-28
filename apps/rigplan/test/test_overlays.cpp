// The Viewport's overlays (M15 slice 8d): a camera's frustum is its own model's field of view, so a
// fisheye's is a fisheye's, and the selection is drawn in its highlight colour.

#include "editorplan.h"
#include "overlays.h"
#include "selection.h"

#include <lain/camera/projection.h>
#include <lain/core/uri.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace math = lain::math;
namespace rp = lain::rigplan;
using Catch::Approx;
using rigplan::Selected;
using rigplan::Selection;

static lain::camera::CameraModel model(lain::camera::Distortion distortion)
{
	lain::camera::CameraModelParameters parameters;
	parameters.image = {1280, 720};
	parameters.intrinsics = {500.0, 500.0, 639.5, 359.5};
	parameters.distortion = distortion;
	lain::camera::ModelResult made = lain::camera::CameraModel::create(parameters);
	REQUIRE(made.model.has_value());
	return *made.model;
}

// The angle from the optical axis of a camera-frame point.
static double offAxis(const math::Vec3d& p)
{
	return std::atan2(std::hypot(p.x, p.y), p.z);
}

TEST_CASE("a frustum's far corners project to the image's corners through the camera's own model", "[rigplan][overlays]")
{
	const lain::camera::CameraModel pinhole = model(lain::camera::NoDistortion{});
	const lain::camera::CameraModel fisheye = model(lain::camera::KannalaBrandt4{0.02, -0.005, 0.001, 0.0});
	const lain::camera::CameraModel barrel = model(lain::camera::BrownConrady5{-0.25, 0.05, 0.001, -0.001, 0.0});
	const std::vector<math::Vec2d> corners{{-0.5, -0.5}, {1279.5, -0.5}, {1279.5, 719.5}, {-0.5, 719.5}};
	for (const lain::camera::CameraModel* m : {&pinhole, &fisheye, &barrel})
	{
		const std::vector<math::Vec3d> outline = rigplan::frustumOutline(*m, 1.5);
		REQUIRE(outline.size() == 4 * std::size_t(rigplan::kFrustumSamples));
		for (const math::Vec3d& p : outline)
			CHECK(math::length(p) == Approx(1.5)); // out to the depth along each ray
		for (std::size_t c = 0; c < corners.size(); ++c)
		{
			const math::Vec3d& p = outline[c * std::size_t(rigplan::kFrustumSamples)];
			const lain::camera::Projection<double> projected = lain::camera::project(*m, p.x, p.y, p.z);
			REQUIRE(projected.ok());
			CHECK(projected.u == Approx(corners[c].x).margin(1e-6));
			CHECK(projected.v == Approx(corners[c].y).margin(1e-6));
		}
	}

	// The same intrinsics through a fisheye reach further off the axis than through a pinhole.
	const double pinholeCorner = offAxis(rigplan::frustumOutline(pinhole, 1.0).front());
	const double fisheyeCorner = offAxis(rigplan::frustumOutline(fisheye, 1.0).front());
	CHECK(fisheyeCorner > pinholeCorner + math::radians(10.0));
}

TEST_CASE("a camera's frustum is drawn from its optical centre to where its model puts the corners", "[rigplan][overlays]")
{
	const rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	const rp::NominalLayout layout = rp::nominalLayout(plan);
	const std::vector<lain::render::Segment> lines = rigplan::overlays(plan, layout, rp::SceneResult{}, {});
	const math::RigidTransformd& pose = *layout.cameras[0].planFromCamera;
	const std::vector<math::Vec3d> outline = rigplan::frustumOutline(*layout.cameras[0].model, 1.0);
	for (int c = 0; c < 4; ++c)
	{
		const math::Vec3d corner = pose.apply(outline[std::size_t(c * rigplan::kFrustumSamples)]);
		const bool drawn = std::any_of(lines.begin(), lines.end(), [&](const lain::render::Segment& s)
									   { return math::length(s.from - pose.translation()) < 1e-12 && math::length(s.to - corner) < 1e-12; });
		CHECK(drawn);
	}
}

TEST_CASE("what is selected is drawn in the highlight colour, and nothing else is", "[rigplan][overlays]")
{
	const rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	const rp::NominalLayout layout = rp::nominalLayout(plan);
	const auto highlighted = [&](const Selection& selection)
	{
		rigplan::OverlayOptions options;
		options.selection = selection;
		std::size_t n = 0;
		for (const lain::render::Segment& s : rigplan::overlays(plan, layout, rp::SceneResult{}, options))
			n += s.colour == rigplan::kSelectedColour ? 1 : 0;
		return n;
	};
	CHECK(highlighted(Selection::nothing()) == 0);
	const std::size_t oneCamera = highlighted(Selection::object(Selected::Camera, editorplan::cam1Id));
	CHECK(oneCamera > 0);
	// The pole's two cameras; the specification all of them; a key alone.
	CHECK(highlighted(Selection::object(Selected::Mount, editorplan::poleId)) == 2 * oneCamera);
	CHECK(highlighted(Selection::specificationOf(rigplan::SpecificationKind::Lens, "4mm")) == 2 * oneCamera);
	CHECK(highlighted(Selection::keyOf(editorplan::boardId, 1)) == 12);				  // its marker's edges
	CHECK(highlighted(Selection::object(Selected::Light, editorplan::lightId)) > 12); // its marker and its cone
}
