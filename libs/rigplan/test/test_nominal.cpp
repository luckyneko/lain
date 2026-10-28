// What an editor needs of a rig plan that a capture does not: the plan as it was meant, drawn whatever
// check() thinks of it (nominalLayout), the aim a rotation makes (aimOf), and what each problem is
// about (PlanDiagnostic::owner).

#include "testplan.h"

#include <lain/rigplan/realisation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

using lain::rigplan::Aim;
using lain::rigplan::NominalLayout;
using lain::rigplan::PlanProblem;
using lain::rigplan::RigPlan;

// Two poses that are one pose, to the last bit of every value.
static void requireSame(const lain::math::RigidTransformd& a, const lain::math::RigidTransformd& b)
{
	CHECK(a.translation() == b.translation());
	CHECK(a.rotation() == b.rotation());
}

static std::vector<double> coefficientsOf(const lain::camera::CameraModel& model)
{
	return std::visit([](const auto& d)
					  {
						  const auto c = d.coefficients();
						  return std::vector<double>(c.begin(), c.end()); },
					  model.distortion());
}

// The test plan with a Kannala-Brandt lens on a second specification, a fixed point turned 37 degrees
// and every aim odd, so nothing about the first plan's round numbers can hide a difference.
static RigPlan oddPlan()
{
	RigPlan p = testplan::plan();
	lain::rigplan::LensSpecification fisheye = testplan::lens("fisheye");
	fisheye.focalLength = lain::core::Length::from<lain::core::Length::Millimetres>(2.0);
	fisheye.distortion = lain::camera::KannalaBrandt4{0.03, -0.01, 0.002, -0.0005};
	p.specifications.lenses.push_back(fisheye);
	p.specifications.cameras.push_back({"hd-fisheye", "hd", "fisheye"});
	p.specifications.sensors[0].body.opticalCentre = {0.01, -0.02, 0.03};
	p.mounts[1].yawDegrees = 37.0;
	p.cameras[0].aim = {12.5, -7.25, 3.125};
	p.cameras[1].camera = "hd-fisheye";
	p.cameras[1].aim = {-101.0, 33.0, -170.0};
	p.lights[0].aim = {7.0, -44.0, 2.0};
	return p;
}

TEST_CASE("a plan's nominal layout is where realise puts everything at scales of 0", "[rigplan][nominal]")
{
	for (RigPlan plan : {testplan::plan(), oddPlan()})
	{
		REQUIRE(lain::rigplan::check(plan).empty());
		const NominalLayout layout = lain::rigplan::nominalLayout(plan); // whatever the plan's scales say

		plan.scales.intrinsic = 0;
		plan.scales.installation = 0;
		plan.scales.locked = true;
		const lain::rigplan::RealisationResult result = lain::rigplan::realise(plan);
		REQUIRE(result.realisation.has_value());
		const lain::rigplan::Realisation& realisation = *result.realisation;

		REQUIRE(layout.mounts.size() == realisation.mounts.size());
		for (std::size_t i = 0; i < layout.mounts.size(); ++i)
		{
			INFO("mount " << i);
			CHECK(layout.mounts[i].id == realisation.mounts[i].id);
			REQUIRE(layout.mounts[i].planFromMount.has_value());
			requireSame(*layout.mounts[i].planFromMount, realisation.mounts[i].planFromMount);
		}
		REQUIRE(layout.cameras.size() == realisation.cameras.size());
		for (std::size_t i = 0; i < layout.cameras.size(); ++i)
		{
			INFO("camera " << layout.cameras[i].name);
			CHECK(layout.cameras[i].id == realisation.cameras[i].id);
			REQUIRE(layout.cameras[i].planFromCamera.has_value());
			requireSame(*layout.cameras[i].planFromCamera, realisation.cameras[i].planFromCamera);
			REQUIRE(layout.cameras[i].model.has_value());
			const lain::camera::Intrinsics& nominal = layout.cameras[i].model->intrinsics();
			const lain::camera::Intrinsics& realised = realisation.cameras[i].model.intrinsics();
			CHECK(nominal.fx == realised.fx);
			CHECK(nominal.fy == realised.fy);
			CHECK(nominal.cx == realised.cx);
			CHECK(nominal.cy == realised.cy);
			CHECK(coefficientsOf(*layout.cameras[i].model) == coefficientsOf(realisation.cameras[i].model));
		}
		REQUIRE(layout.lights.size() == realisation.lights.size());
		for (std::size_t i = 0; i < layout.lights.size(); ++i)
		{
			REQUIRE(layout.lights[i].planFromLight.has_value());
			requireSame(*layout.lights[i].planFromLight, realisation.lights[i].planFromLight);
		}
	}
}

// A placement's rotation, and the rotation an aim makes, as matrices: a quaternion and its negation
// are one rotation.
static lain::math::Mat3d turned(const Aim& aim)
{
	return lain::math::mat3_cast(lain::rigplan::mountFromPlacement({}, aim).rotation());
}

static double largestDifference(const lain::math::Mat3d& a, const lain::math::Mat3d& b)
{
	double worst = 0;
	for (int c = 0; c < 3; ++c)
	{
		for (int r = 0; r < 3; ++r)
			worst = std::max(worst, std::abs(a[c][r] - b[c][r]));
	}
	return worst;
}

// The problems check() finds about one object.
static std::vector<PlanProblem> problemsAbout(const RigPlan& plan, const lain::core::Uuid& owner)
{
	std::vector<PlanProblem> found;
	for (const lain::rigplan::PlanDiagnostic& d : lain::rigplan::check(plan))
	{
		if (d.owner == owner)
			found.push_back(d.problem);
	}
	return found;
}

TEST_CASE("a nominal layout draws the rest of a plan that check refuses", "[rigplan][nominal]")
{
	SECTION("a camera inside its pole keeps its pose and its model")
	{
		RigPlan plan = testplan::plan();
		plan.cameras[0].offset = {0.0, 0.0, 1.5}; // on the pole's axis
		CHECK(problemsAbout(plan, testplan::cam0Id) == std::vector<PlanProblem>{PlanProblem::InsidePole});
		CHECK_FALSE(lain::rigplan::realise(plan).realisation.has_value());

		const NominalLayout layout = lain::rigplan::nominalLayout(plan);
		REQUIRE(layout.cameras[0].planFromCamera.has_value());
		CHECK(layout.cameras[0].model.has_value());
		const lain::math::Vec3d centre = layout.cameras[0].planFromCamera->translation();
		CHECK(std::abs(centre.x - 2.0) < 1e-12); // on the pole's axis, as placed
		CHECK(std::abs(centre.y - 3.0) < 1e-12);
	}
	SECTION("a camera naming a specification the plan lacks keeps a pose at its mounting point, and no model")
	{
		RigPlan plan = testplan::plan();
		plan.specifications.sensors[0].body.opticalCentre = {0.0, 0.0, 0.05};
		plan.cameras[1].camera = "no such camera";
		CHECK(problemsAbout(plan, testplan::cam1Id) == std::vector<PlanProblem>{PlanProblem::UnknownSpecification});

		const NominalLayout layout = lain::rigplan::nominalLayout(plan);
		REQUIRE(layout.cameras[1].planFromCamera.has_value());
		CHECK_FALSE(layout.cameras[1].model.has_value());
		// Within rounding: the layout's pose takes the step to an optical centre of 0, and a rigid transform
		// renormalises its rotation as it is made.
		const lain::math::RigidTransformd mounting = *layout.mounts[0].planFromMount *
													 lain::rigplan::mountFromPlacement(plan.cameras[1].offset, plan.cameras[1].aim);
		CHECK(lain::math::length(layout.cameras[1].planFromCamera->translation() - mounting.translation()) < 1e-12);
		CHECK(largestDifference(lain::math::mat3_cast(layout.cameras[1].planFromCamera->rotation()), lain::math::mat3_cast(mounting.rotation())) < 1e-12);
		// The others are as they were, optical centre and all.
		REQUIRE(layout.cameras[0].planFromCamera.has_value());
		CHECK(layout.cameras[0].model.has_value());
	}
	SECTION("a mount with no position takes its cameras and lights with it, and nothing else")
	{
		RigPlan plan = testplan::plan();
		std::get<lain::rigplan::FixedPoint>(plan.mounts[1].kind).position.z = std::numeric_limits<double>::quiet_NaN();
		CHECK(problemsAbout(plan, testplan::wallId) == std::vector<PlanProblem>{PlanProblem::InvalidMount});

		const NominalLayout layout = lain::rigplan::nominalLayout(plan);
		CHECK(layout.mounts[0].planFromMount.has_value());
		CHECK_FALSE(layout.mounts[1].planFromMount.has_value());
		CHECK(layout.cameras[0].planFromCamera.has_value());
		CHECK(layout.cameras[1].planFromCamera.has_value());
		CHECK_FALSE(layout.cameras[2].planFromCamera.has_value()); // on the wall bracket
		CHECK(layout.cameras[2].model.has_value());				   // a model is the specification's, wherever it stands
		CHECK_FALSE(layout.lights[0].planFromLight.has_value());
	}
}

TEST_CASE("aimOf undoes an aim", "[rigplan][nominal]")
{
	int checked = 0;
	for (int pan = -170; pan <= 170; pan += 37)
	{
		for (int tilt = -85; tilt <= 85; tilt += 17)
		{
			for (int roll = -170; roll <= 170; roll += 41)
			{
				const Aim aim{double(pan), double(tilt), double(roll)};
				const Aim back = lain::rigplan::aimOf(lain::rigplan::mountFromPlacement({}, aim).rotation());
				INFO("pan " << pan << ", tilt " << tilt << ", roll " << roll);
				CHECK(std::abs(back.panDegrees - aim.panDegrees) < 1e-9);
				CHECK(std::abs(back.tiltDegrees - aim.tiltDegrees) < 1e-9);
				CHECK(std::abs(back.rollDegrees - aim.rollDegrees) < 1e-9);
				++checked;
			}
		}
	}
	CHECK(checked == 10 * 11 * 9);

	SECTION("an aim outside the ranges comes back as the same rotation inside them")
	{
		const Aim wide{200.0, 30.0, -190.0};
		const Aim back = lain::rigplan::aimOf(lain::rigplan::mountFromPlacement({}, wide).rotation());
		CHECK(std::abs(back.panDegrees - -160.0) < 1e-9);
		CHECK(std::abs(back.rollDegrees - 170.0) < 1e-9);
		CHECK(largestDifference(turned(back), turned(wide)) < 1e-12);
	}
	SECTION("straight up or down, the roll is 0 and the pan carries it")
	{
		for (const double tilt : {90.0, -90.0})
		{
			INFO("tilt " << tilt);
			const Aim aim{10.0, tilt, 30.0};
			const Aim back = lain::rigplan::aimOf(lain::rigplan::mountFromPlacement({}, aim).rotation());
			CHECK(std::abs(back.tiltDegrees - tilt) < 1e-6);
			CHECK(back.rollDegrees == 0.0);
			CHECK(std::abs(back.panDegrees - (tilt > 0 ? 40.0 : -20.0)) < 1e-6);
			CHECK(largestDifference(turned(back), turned(aim)) < 1e-12);
		}
	}
}

TEST_CASE("each problem check finds says which object it is about", "[rigplan][nominal]")
{
	RigPlan plan = testplan::plan();
	plan.cameras[2].name = "a/b";							// a camera's
	plan.mounts[0].tolerance.yawDegrees = -1.0;				// a mount's
	plan.lights[0].light = "no such light";					// a light's
	plan.specifications.lenses[0].focalLengthTolerance = 2; // a specification's: none
	plan.timeline.frames = 0;								// the plan's: none

	std::vector<std::pair<PlanProblem, std::optional<lain::core::Uuid>>> found;
	for (const lain::rigplan::PlanDiagnostic& d : lain::rigplan::check(plan))
		found.emplace_back(d.problem, d.owner);
	const std::vector<std::pair<PlanProblem, std::optional<lain::core::Uuid>>> expected = {
		{PlanProblem::InvalidSpecification, std::nullopt},
		{PlanProblem::InvalidTolerance, testplan::poleId},
		{PlanProblem::InvalidName, testplan::cam2Id},
		{PlanProblem::UnknownSpecification, testplan::lightId},
		{PlanProblem::InvalidTimeline, std::nullopt},
	};
	CHECK(found == expected);
}
