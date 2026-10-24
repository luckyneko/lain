// Realisation: one physical build of a rig plan, drawn from its seed.

#include "testplan.h"

#include <lain/rigplan/realisation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

using lain::math::Vec3d;
using lain::rigplan::RealisationResult;
using lain::rigplan::RealisedCamera;
using lain::rigplan::RigPlan;

static lain::rigplan::Realisation realised(const RigPlan& plan)
{
	RealisationResult result = lain::rigplan::realise(plan);
	for (const auto& d : result.diagnostics)
		UNSCOPED_INFO(d.detail);
	REQUIRE(result.realisation.has_value());
	return std::move(*result.realisation);
}

static const RealisedCamera& cameraNamed(const lain::rigplan::Realisation& r, const std::string& name)
{
	const auto it = std::find_if(r.cameras.begin(), r.cameras.end(), [&](const RealisedCamera& c)
								 { return c.name == name; });
	REQUIRE(it != r.cameras.end());
	return *it;
}

static std::vector<double> distortionOf(const lain::camera::CameraModel& model)
{
	return std::visit(
		[](const auto& d)
		{
			const auto c = d.coefficients();
			return std::vector<double>(c.begin(), c.end());
		},
		model.distortion());
}

// The same unit, to the last bit: model and pose.
static void requireIdentical(const RealisedCamera& a, const RealisedCamera& b)
{
	const lain::camera::Intrinsics& ia = a.model.intrinsics();
	const lain::camera::Intrinsics& ib = b.model.intrinsics();
	REQUIRE(ia.fx == ib.fx);
	REQUIRE(ia.fy == ib.fy);
	REQUIRE(ia.cx == ib.cx);
	REQUIRE(ia.cy == ib.cy);
	REQUIRE(distortionOf(a.model) == distortionOf(b.model));
	REQUIRE(a.planFromCamera.rotation() == b.planFromCamera.rotation());
	REQUIRE(a.planFromCamera.translation() == b.planFromCamera.translation());
}

static bool near(const Vec3d& a, const Vec3d& b, double tolerance = 1e-12)
{
	return lain::math::length(a - b) < tolerance;
}

TEST_CASE("a draw is pinned to its canonical text, the same everywhere", "[rigplan][realisation]")
{
	// Computed outside lain, with Python's hashlib, from "lain-rigplan-draw/1\n<seed>\n<uuid>\n<key>\n".
	const lain::core::Uuid owner = testplan::id("0193a6f0-8c1a-7b2e-9d4c-5e6f7a8b9c0d");
	REQUIRE(lain::rigplan::unitDeviation(7, owner, "focalLength") == 0x1.f8d7f2f28cb6cp-1);
	REQUIRE(lain::rigplan::unitDeviation(7, owner, "principalPoint.x") == 0x1.39ceb1a5c8cc4p-1);
	REQUIRE(lain::rigplan::unitDeviation(8, owner, "focalLength") == 0x1.482cf8f876832p-1);
}

TEST_CASE("draws lie in [-1, 1) and take both signs", "[rigplan][realisation]")
{
	bool negative = false, positive = false;
	for (std::uint64_t seed = 0; seed < 2000; ++seed)
	{
		const double u = lain::rigplan::unitDeviation(seed, testplan::cam0Id, "pan");
		REQUIRE(u >= -1.0);
		REQUIRE(u < 1.0);
		negative = negative || u < 0;
		positive = positive || u > 0;
	}
	REQUIRE(negative);
	REQUIRE(positive);
}

TEST_CASE("an aim is roll, then tilt, then pan, in the mount's frame", "[rigplan][realisation]")
{
	const Vec3d forward{0.0, 0.0, 1.0}, right{1.0, 0.0, 0.0}, down{0.0, 1.0, 0.0}; // the camera frame
	using lain::rigplan::mountFromPlacement;

	const auto rest = mountFromPlacement({0.0, 0.0, 0.0}, {});
	REQUIRE(near(rest.rotate(forward), {0.0, 1.0, 0.0}));
	REQUIRE(near(rest.rotate(right), {1.0, 0.0, 0.0}));
	REQUIRE(near(rest.rotate(down), {0.0, 0.0, -1.0}));

	REQUIRE(near(mountFromPlacement({}, {90.0, 0.0, 0.0}).rotate(forward), {-1.0, 0.0, 0.0})); // pan turns left
	REQUIRE(near(mountFromPlacement({}, {0.0, 90.0, 0.0}).rotate(forward), {0.0, 0.0, 1.0}));  // tilt looks up
	// Roll is clockwise as seen from behind: the image's right turns down.
	REQUIRE(near(mountFromPlacement({}, {0.0, 0.0, 90.0}).rotate(right), {0.0, 0.0, -1.0}));
	// Pan comes last, so it does not change what tilt means: tilted down, then panned left.
	const double c = std::cos(lain::math::radians(30.0)), s = std::sin(lain::math::radians(30.0));
	REQUIRE(near(mountFromPlacement({}, {90.0, -30.0, 0.0}).rotate(forward), {-c, 0.0, -s}));
}

TEST_CASE("a hand-placed camera is where it was put", "[rigplan][realisation]")
{
	// A pole at (2, 3) turned 90 degrees, a camera 1.5 m up it and 12.5 cm out along its +Y, tilted 10
	// degrees down, its optical centre 5 cm in front of its mounting point. Turned 90, the pole's +Y is
	// the plan's -X.
	RigPlan p = testplan::plan();
	p.scales = {};
	p.mounts[0].yawDegrees = 90.0;
	p.specifications.sensors[0].body.opticalCentre = {0.0, 0.0, 0.05};
	p.cameras[0].aim = {0.0, -10.0, 0.0};
	const lain::rigplan::Realisation r = realised(p); // held: cameraNamed returns a reference into it
	const RealisedCamera& cam0 = cameraNamed(r, "cam0");

	const double c = std::cos(lain::math::radians(10.0)), s = std::sin(lain::math::radians(10.0));
	const Vec3d forward{-c, 0.0, -s};
	REQUIRE(near(cam0.planFromCamera.rotate({0.0, 0.0, 1.0}), forward));
	REQUIRE(near(cam0.planFromCamera.translation(), Vec3d{1.875, 3.0, 1.5} + 0.05 * forward));
}

TEST_CASE("at scales of 0 a realisation is the nominal rig plan exactly", "[rigplan][realisation]")
{
	RigPlan p = testplan::plan();
	p.scales = {};
	const lain::rigplan::Realisation zero = realised(p);

	// Every model is the nominal model, to the last bit.
	const auto nominal = lain::rigplan::nominalModel(p.specifications.sensors[0], p.specifications.lenses[0]);
	for (const RealisedCamera& camera : zero.cameras)
	{
		REQUIRE(camera.model.intrinsics().fx == nominal.model->intrinsics().fx);
		REQUIRE(camera.model.intrinsics().cx == nominal.model->intrinsics().cx);
		REQUIRE(camera.model.intrinsics().cy == nominal.model->intrinsics().cy);
		REQUIRE(distortionOf(camera.model) == distortionOf(*nominal.model));
	}

	// And every pose is the plan's: the same as a plan with no tolerances at all, realised at full scale.
	RigPlan exact = testplan::plan();
	for (auto& mount : exact.mounts)
		mount.tolerance = {};
	for (auto& camera : exact.cameras)
		camera.tolerance = {};
	exact.specifications.lenses[0].focalLengthTolerance = 0;
	exact.specifications.lenses[0].distortionTolerance = 0;
	exact.specifications.sensors[0].principalPointTolerance = {};
	const lain::rigplan::Realisation untoleranced = realised(exact);
	for (std::size_t i = 0; i < zero.cameras.size(); ++i)
		requireIdentical(zero.cameras[i], untoleranced.cameras[i]);
	REQUIRE(zero.cameras[0].planFromCamera.translation() == Vec3d{2.0, 3.125, 1.5});
}

TEST_CASE("an edit to one camera re-rolls no other", "[rigplan][realisation]")
{
	const RigPlan base = testplan::plan();
	const lain::rigplan::Realisation before = realised(base);

	SECTION("adding a camera")
	{
		RigPlan p = base;
		p.cameras.insert(p.cameras.begin(), testplan::camera(testplan::id("0193a6f0-0000-7000-8000-00000000c009"), "cam9",
															 testplan::poleId, {0.0, 0.125, 0.5}, {}));
		const lain::rigplan::Realisation after = realised(p);
		for (const char* name : {"cam0", "cam1", "cam2"})
			requireIdentical(cameraNamed(before, name), cameraNamed(after, name));
	}
	SECTION("removing a camera")
	{
		RigPlan p = base;
		p.cameras.erase(p.cameras.begin() + 1);
		const lain::rigplan::Realisation after = realised(p);
		for (const char* name : {"cam0", "cam2"})
			requireIdentical(cameraNamed(before, name), cameraNamed(after, name));
	}
	SECTION("reordering the cameras")
	{
		RigPlan p = base;
		std::reverse(p.cameras.begin(), p.cameras.end());
		const lain::rigplan::Realisation after = realised(p);
		for (const char* name : {"cam0", "cam1", "cam2"})
			requireIdentical(cameraNamed(before, name), cameraNamed(after, name));
	}
	SECTION("renaming a camera re-rolls not even that one")
	{
		RigPlan p = base;
		p.cameras[1].name = "north-high";
		requireIdentical(cameraNamed(before, "cam1"), cameraNamed(realised(p), "north-high"));
	}
	SECTION("a duplicate is a new unit, with a draw of its own")
	{
		RigPlan p = base;
		lain::rigplan::PlacedCamera copy = p.cameras[1];
		copy.id = testplan::id("0193a6f0-0000-7000-8000-00000000c00a");
		copy.name = "cam1-copy";
		p.cameras.push_back(copy);
		const lain::rigplan::Realisation after = realised(p);
		REQUIRE(cameraNamed(after, "cam1-copy").model.intrinsics().fx != cameraNamed(after, "cam1").model.intrinsics().fx);
	}
}

TEST_CASE("sweeping a scale degrades the same build steadily", "[rigplan][realisation]")
{
	// A seed fixes each deviation's direction, so every drawn parameter moves in proportion to the
	// scale. The intrinsics are those parameters, so they move exactly in proportion. A pose is built
	// from them, and a rotation's deviation turns an offset's deviation, so it moves in proportion to
	// first order: the second-order part is a yaw of about 0.02 rad times an offset of about 5 mm.
	RigPlan p = testplan::plan();
	p.scales = {};
	const RealisedCamera nominal = cameraNamed(realised(p), "cam1");
	p.scales.intrinsic = 1.0;
	const RealisedCamera full = cameraNamed(realised(p), "cam1");
	p.scales.intrinsic = 0.5;
	const RealisedCamera half = cameraNamed(realised(p), "cam1");

	const double fullShift = full.model.intrinsics().fx - nominal.model.intrinsics().fx;
	const double halfShift = half.model.intrinsics().fx - nominal.model.intrinsics().fx;
	REQUIRE(fullShift != 0.0);
	REQUIRE(std::abs(halfShift - 0.5 * fullShift) < 1e-9);
	const Vec3d fullMove = full.planFromCamera.translation() - nominal.planFromCamera.translation();
	const Vec3d halfMove = half.planFromCamera.translation() - nominal.planFromCamera.translation();
	REQUIRE(lain::math::length(fullMove) > 0.0);
	REQUIRE(near(halfMove, 0.5 * fullMove, 1e-4));
	REQUIRE(lain::math::dot(halfMove, fullMove) > 0.0); // the same direction
}

TEST_CASE("at full scale every parameter stays within its tolerance", "[rigplan][realisation]")
{
	RigPlan p = testplan::plan();
	const lain::rigplan::Realisation r = realised(p);
	const auto nominal = lain::rigplan::nominalModel(p.specifications.sensors[0], p.specifications.lenses[0]);
	const double fx = nominal.model->intrinsics().fx, cx = nominal.model->intrinsics().cx;
	for (const RealisedCamera& camera : r.cameras)
	{
		REQUIRE(std::abs(camera.model.intrinsics().fx / fx - 1.0) <= 0.01);
		REQUIRE(camera.model.intrinsics().fx == camera.model.intrinsics().fy); // one focal length moves both
		REQUIRE(std::abs(camera.model.intrinsics().cx - cx) <= 10.0);		   // 20 um over 2 um pixels
	}
	// A pole moves across the floor, never into it.
	const Vec3d pole = r.mounts[0].planFromMount.translation();
	REQUIRE(std::abs(pole.x - 2.0) <= 0.010);
	REQUIRE(std::abs(pole.y - 3.0) <= 0.010);
	REQUIRE(pole.z == 0.0);
	const Vec3d wall = r.mounts[1].planFromMount.translation();
	REQUIRE(wall.z != 2.8); // a fixed point's height varies
	REQUIRE(std::abs(wall.z - 2.8) <= 0.010);
}

TEST_CASE("a draw that makes no camera model refuses the build, naming the camera", "[rigplan][realisation]")
{
	// A wide lens just inside folding. Under k1 alone a ray reaches a distorted radius of at most
	// (2/3) sqrt(-1 / (3 k1)): 1.57 for k1 = -0.06, past this 1 mm lens's corner at 1.47 (fx = 500 on
	// a 1280 x 720 sensor), so the nominal model exists. A unit more than 15% stronger falls short of
	// the corner. Find a seed whose draw for this camera's distortion is at least 50% stronger, so the
	// refusal is certain rather than a matter of luck.
	RigPlan p = testplan::plan();
	p.specifications.lenses[0].focalLength = testplan::Length::from<testplan::Length::Millimetres>(1.0);
	p.specifications.lenses[0].distortion = lain::camera::BrownConrady5{-0.06, 0.0, 0.0, 0.0, 0.0};
	p.specifications.lenses[0].distortionTolerance = 1.0;
	p.cameras.resize(1);
	p.lights.clear();
	while (lain::rigplan::unitDeviation(p.seed, testplan::cam0Id, "distortion") < 0.5)
		++p.seed;

	const RealisationResult refused = lain::rigplan::realise(p);
	REQUIRE_FALSE(refused.realisation.has_value());
	REQUIRE(refused.diagnostics.size() == 1);
	REQUIRE(refused.diagnostics[0].problem == lain::rigplan::RealisationProblem::InvalidRealisedModel);
	REQUIRE(refused.diagnostics[0].detail.find("'cam0'") != std::string::npos);

	p.scales = {}; // the nominal build is fine: it is this unit that cannot exist
	REQUIRE(lain::rigplan::realise(p).realisation.has_value());
}

TEST_CASE("a light shines from where its mount and placement put it", "[rigplan][realisation]")
{
	RigPlan p = testplan::plan();
	p.scales = {};
	const lain::rigplan::Realisation r = realised(p);
	// The east wall's bracket at (6, 3, 2.8), turned 90 so its +Y is the plan's -X, the spot tilted
	// 45 degrees down.
	const double h = std::sqrt(0.5);
	REQUIRE(near(r.lights[0].planFromLight.translation(), {6.0, 3.0, 2.8}));
	REQUIRE(near(r.lights[0].planFromLight.rotate({0.0, 0.0, 1.0}), {-h, 0.0, -h}));
}

TEST_CASE("a rig plan with a problem is not realised", "[rigplan][realisation]")
{
	RigPlan p = testplan::plan();
	p.cameras[0].mount = {};
	const RealisationResult result = lain::rigplan::realise(p);
	REQUIRE_FALSE(result.realisation.has_value());
	REQUIRE(result.diagnostics.front().problem == lain::rigplan::RealisationProblem::InvalidPlan);
}
