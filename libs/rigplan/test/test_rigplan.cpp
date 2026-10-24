// A rig plan's checks: what must hold before it can be realised.

#include "testplan.h"

#include <lain/rigplan/rigplan.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using lain::rigplan::PlanDiagnostic;
using lain::rigplan::PlanProblem;
using lain::rigplan::RigPlan;

static std::vector<PlanProblem> problems(const RigPlan& plan)
{
	std::vector<PlanProblem> out;
	for (const PlanDiagnostic& d : lain::rigplan::check(plan))
		out.push_back(d.problem);
	return out;
}

TEST_CASE("the test plan can be realised", "[rigplan][plan]")
{
	REQUIRE(lain::rigplan::check(testplan::plan()).empty());
}

TEST_CASE("a specification's problem is the plan's", "[rigplan][plan]")
{
	RigPlan p = testplan::plan();
	p.specifications.lenses[0].focalLengthTolerance = -0.1;
	REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidSpecification});
}

TEST_CASE("every mount, camera and light has an identity of its own", "[rigplan][plan]")
{
	SECTION("the nil uuid is no identity")
	{
		RigPlan p = testplan::plan();
		p.lights[0].id = {};
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::MissingIdentity});
	}
	SECTION("a camera may not share a mount's")
	{
		// A draw is keyed by its owner's identity, so two owners sharing one would draw alike.
		RigPlan p = testplan::plan();
		p.cameras[2].id = testplan::poleId;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::DuplicateIdentity});
	}
}

TEST_CASE("a camera's name is its capture folder's, so unique ignoring case and valid everywhere", "[rigplan][plan]")
{
	SECTION("unnamed")
	{
		RigPlan p = testplan::plan();
		p.cameras[0].name.clear();
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::Unnamed});
	}
	SECTION("two folders on a case-insensitive file system")
	{
		RigPlan p = testplan::plan();
		p.cameras[1].name = "CAM0";
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::DuplicateName});
	}
	SECTION("not a folder's name on Windows")
	{
		RigPlan p = testplan::plan();
		p.cameras[1].name = "north:1";
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidName});
	}
}

TEST_CASE("a file name valid on every platform", "[rigplan][plan]")
{
	for (const char* name : {"cam0", "north-01", "Cam 3", "a.b", "COM0", "COM10", "console"})
	{
		INFO(name);
		REQUIRE(lain::rigplan::validFileName(name));
	}
	for (const char* name : {"", ".", "..", "a/b", "a\\b", "a:b", "x?", "<x>", "a|b", "a*", "say \"hi\"", "trail ",
							 "trail.", "CON", "con.json", "nul", "COM1", "lpt9.txt", "\001a"})
	{
		INFO(name);
		REQUIRE_FALSE(lain::rigplan::validFileName(name));
	}
}

TEST_CASE("what a camera or light names must exist", "[rigplan][plan]")
{
	RigPlan p = testplan::plan();
	p.cameras[0].camera = "hd-8mm";
	p.cameras[1].mount = testplan::lightId; // an identity, but not a mount's
	p.lights[0].light = "softbox";
	REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::UnknownSpecification, PlanProblem::UnknownMount,
													PlanProblem::UnknownSpecification});
}

TEST_CASE("geometry is finite, a pole has extent, and tolerances are never negative", "[rigplan][plan]")
{
	RigPlan p = testplan::plan();
	std::get<lain::rigplan::Pole>(p.mounts[0].kind).height = 0.0;
	p.cameras[0].aim.tiltDegrees = std::numeric_limits<double>::infinity();
	p.cameras[1].tolerance.panDegrees = -0.5;
	REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidMount, PlanProblem::InvalidPlacement,
													PlanProblem::InvalidTolerance});
}

TEST_CASE("a camera or light inside its own pole is refused, as far as its tolerances could move it", "[rigplan][plan]")
{
	// The pole is 5 cm across; cam0 has 5 mm on each axis and half a degree each way of tolerance.
	RigPlan p = testplan::plan();
	REQUIRE(problems(p).empty());

	SECTION("on the pole's axis")
	{
		p.cameras[0].offset = {0.0, 0.0, 1.5};
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InsidePole});
	}
	SECTION("outside the pole, but not by as much as its tolerances")
	{
		p.cameras[0].offset = {0.0, 0.03, 1.5}; // 5 mm out, and its offset tolerance alone reaches 8.7 mm
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InsidePole});
		p.cameras[0].tolerance = {};
		REQUIRE(problems(p).empty());
	}
	SECTION("its mounting point outside, its optical centre inside")
	{
		p.cameras[0].offset = {0.0, 0.125, 1.5};
		p.specifications.sensors[0].body.opticalCentre = {0.0, 0.0, -0.125}; // 12.5 cm behind it: on the axis
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InsidePole, PlanProblem::InsidePole});
	}
	SECTION("above the pole's top, on its axis")
	{
		p.cameras[0].offset = {0.0, 0.0, 2.6};
		REQUIRE(problems(p).empty());
	}
	SECTION("a light inside it")
	{
		p.lights[0].mount = testplan::poleId;
		p.lights[0].offset = {0.0, 0.01, 1.0};
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InsidePole});
	}
}

TEST_CASE("a capture has frames, and every scale lies between 0 and 1", "[rigplan][plan]")
{
	RigPlan p = testplan::plan();
	p.timeline.frames = 0;
	p.scales.noise = 1.5;
	REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidTimeline, PlanProblem::InvalidScale});
}

TEST_CASE("installation follows intrinsic until a user separates them", "[rigplan][plan]")
{
	lain::rigplan::Scales scales;
	scales.intrinsic = 0.25;
	scales.installation = 0.75;
	REQUIRE(scales.effectiveInstallation() == 0.25);
	scales.locked = false;
	REQUIRE(scales.effectiveInstallation() == 0.75);
}
