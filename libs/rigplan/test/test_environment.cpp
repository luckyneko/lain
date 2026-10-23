// The room, planes, boxes and props, their checks, and the matrix that puts a prop's mesh in the plan.

#include "testplan.h"

#include <lain/rigplan/environment.h>
#include <lain/rigplan/rigplan.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using lain::math::Vec3d;
using lain::math::Vec4d;
using lain::rigplan::PlanProblem;

static std::vector<PlanProblem> problems(const lain::rigplan::RigPlan& plan)
{
	std::vector<PlanProblem> out;
	for (const auto& d : lain::rigplan::check(plan))
		out.push_back(d.problem);
	return out;
}

static Vec3d mapped(const lain::math::Mat4d& m, const Vec3d& p)
{
	const Vec4d q = m * Vec4d(p, 1.0);
	return {q.x, q.y, q.z};
}

static bool near(const Vec3d& a, const Vec3d& b)
{
	return lain::math::length(a - b) < 1e-12;
}

TEST_CASE("a furnished rig plan can be realised", "[rigplan][environment]")
{
	REQUIRE(lain::rigplan::check(testplan::furnished()).empty());
}

TEST_CASE("the room, planes and boxes have extent, and a prop a mesh and a unit", "[rigplan][environment]")
{
	SECTION("a room of no height")
	{
		auto p = testplan::furnished();
		p.room->size.z = 0.0;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
	}
	SECTION("a plane of negative width")
	{
		auto p = testplan::furnished();
		p.planes[0].size.x = -1.0;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
	}
	SECTION("a prop with no mesh, or no unit")
	{
		auto p = testplan::furnished();
		p.props[0].mesh = {};
		p.boxes[0].size.y = 0.0;
		auto q = testplan::furnished();
		q.props[0].unitMetres = 0.0;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment, PlanProblem::InvalidEnvironment});
		REQUIRE(problems(q) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
	}
	SECTION("a box shares no identity with a camera")
	{
		auto p = testplan::furnished();
		p.boxes[0].id = testplan::cam0Id;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::DuplicateIdentity});
	}
}

TEST_CASE("a material is a colour, a picture or a procedural texture that can be one", "[rigplan][environment]")
{
	SECTION("a picture needs a file and a tile")
	{
		auto p = testplan::furnished();
		std::get<lain::rigplan::ImageTexture>(p.room->north.material.surface).tile.y = 0.0;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
		std::get<lain::rigplan::ImageTexture>(p.room->north.material.surface) = {};
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
	}
	SECTION("a procedural texture needs a cell")
	{
		auto p = testplan::furnished();
		std::get<lain::image::ProceduralTexture>(p.room->floor.material.surface).cell = 0.0;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
	}
	SECTION("specular light is never negative")
	{
		auto p = testplan::furnished();
		p.planes[0].material.specular.g = -0.1f;
		REQUIRE(problems(p) == std::vector<PlanProblem>{PlanProblem::InvalidEnvironment});
	}
	SECTION("a surface switched off is not judged")
	{
		auto p = testplan::furnished();
		p.room->ceiling.material.surface = lain::rigplan::ImageTexture{}; // no file, but no ceiling either
		REQUIRE(problems(p).empty());
	}
}

TEST_CASE("a prop's mesh is put in the plan frame once: its up axis, its unit, its yaw, its place", "[rigplan][environment]")
{
	lain::rigplan::Prop prop;
	SECTION("a Y-up mesh stands up in the Z-up plan")
	{
		const auto m = lain::rigplan::planFromProp(prop);
		REQUIRE(near(mapped(m, {0.0, 1.0, 0.0}), {0.0, 0.0, 1.0}));	 // the file's up is the plan's up
		REQUIRE(near(mapped(m, {0.0, 0.0, -1.0}), {0.0, 1.0, 0.0})); // the file's forward (-Z) is the plan's
		REQUIRE(near(mapped(m, {1.0, 0.0, 0.0}), {1.0, 0.0, 0.0}));
	}
	SECTION("a Z-up mesh is already in the plan's axes")
	{
		prop.up = lain::rigplan::UpAxis::Z;
		REQUIRE(near(mapped(lain::rigplan::planFromProp(prop), {1.0, 2.0, 3.0}), {1.0, 2.0, 3.0}));
	}
	SECTION("centimetres, turned a quarter left and stood elsewhere")
	{
		prop.up = lain::rigplan::UpAxis::Z;
		prop.unitMetres = 0.01;
		prop.yawDegrees = 90.0;
		prop.position = {5.0, 6.0, 0.0};
		// 100 units along the file's +X is a metre, which a quarter turn left points along +Y.
		REQUIRE(near(mapped(lain::rigplan::planFromProp(prop), {100.0, 0.0, 0.0}), {5.0, 7.0, 0.0}));
		REQUIRE(near(mapped(lain::rigplan::planFromProp(prop), {0.0, 0.0, 170.0}), {5.0, 6.0, 1.7}));
	}
	SECTION("both conventions are right-handed, so no mesh comes out mirrored")
	{
		REQUIRE(lain::rigplan::conventionOf(lain::rigplan::UpAxis::Y).rightHanded());
		REQUIRE(lain::rigplan::conventionOf(lain::rigplan::UpAxis::Z).rightHanded());
	}
}
