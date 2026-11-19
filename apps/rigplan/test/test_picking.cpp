// The Viewport's picking (M15 slice 8d): a click on what the Viewport draws selects it, the nearest thing
// along the ray winning and the room only when nothing else is there. Drawn through the production render
// scene of the nominal layout, as the pane draws it.

#include "document.h"
#include "editorplan.h"
#include "orbit.h"
#include "picking.h"
#include "selection.h"

#include <lain/core/uri.h>
#include <lain/io/mesh/codecs.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
namespace math = lain::math;
namespace rp = lain::rigplan;
using rigplan::Selected;
using rigplan::Selection;

static const lain::camera::ImageGeometry kPane{960, 720};

// The editor's plan, its prop's cube on disk, and the scene the Viewport draws of it at frame 0. Its
// board is not drawn (this executable has no board renderer), so it is picked by its keys.
struct Drawn
{
	rp::RigPlan plan;
	rp::NominalLayout layout;
	rp::SceneResult scene;
};

static Drawn drawn(rp::RigPlan plan)
{
	lain::io::mesh::registerMeshCodecs();
	Drawn d{std::move(plan), {}, {}};
	d.layout = rp::nominalLayout(d.plan);
	const rp::AssetsResult assets = rp::loadSceneAssets(d.plan);
	d.scene = rp::renderScene(d.plan, d.layout, assets.assets, 0);
	return d;
}

static rp::RigPlan editorPlan()
{
	const fs::path cube = lain::testing::scratchDir() / "picking" / "chair.obj";
	editorplan::writeCube(cube);
	return editorplan::plan(lain::core::Uri::fromPath(cube));
}

// Where `view` puts plan point `p`, in pixels.
static math::Vec2d project(const lain::render::View& view, const math::Vec3d& p)
{
	const math::Vec3d c = view.worldFromCamera.inverse().apply(p);
	return {view.intrinsics.fx * c.x / c.z + view.intrinsics.cx, view.intrinsics.fy * c.y / c.z + view.intrinsics.cy};
}

TEST_CASE("a click on each object's projected centre picks it", "[rigplan][picking]")
{
	const Drawn d = drawn(editorPlan());
	REQUIRE(rp::check(d.plan).empty());
	rigplan::Orbit orbit;
	orbit.target = {0.0, 0.0, 1.0};
	orbit.distance = 9.0;
	orbit.pitchDegrees = 35.0;
	const lain::render::View view = rigplan::viewOf(orbit, kPane);

	struct Case
	{
		std::string what;
		math::Vec3d centre;
		Selection expected;
	};
	const auto cameraCentre = [&](std::size_t i)
	{ return d.layout.cameras[i].planFromCamera->translation(); };
	const std::vector<Case> cases{
		{"the pole", {-2.0, 0.0, 1.0}, Selection::object(Selected::Mount, editorplan::poleId)},
		{"cam1", cameraCentre(0), Selection::object(Selected::Camera, editorplan::cam1Id)},
		{"cam2", cameraCentre(1), Selection::object(Selected::Camera, editorplan::cam2Id)},
		{"the fixed point", d.layout.mounts[1].planFromMount->translation(), Selection::object(Selected::Mount, editorplan::bracketId)},
		{"the spot", d.layout.lights[0].planFromLight->translation(), Selection::object(Selected::Light, editorplan::lightId)},
		{"the plane", d.plan.planes[0].centre, Selection::object(Selected::Plane, editorplan::planeId)},
		{"the box", d.plan.boxes[0].centre, Selection::object(Selected::Box, editorplan::boxId)},
		{"the prop", d.plan.props[0].position + math::Vec3d{0.0, 0.0, 0.2}, Selection::object(Selected::Prop, editorplan::propId)},
		{"the board's first key", d.plan.boards[0].path.keys[0].position, Selection::keyOf(editorplan::boardId, 0)},
		{"the board's second key", d.plan.boards[0].path.keys[1].position, Selection::keyOf(editorplan::boardId, 1)},
		{"the floor", {3.5, 2.5, 0.0}, Selection::room()},
	};
	for (const Case& c : cases)
	{
		const Selection picked = rigplan::pick(d.plan, d.layout, d.scene, rigplan::rayThrough(view, project(view, c.centre)));
		INFO(c.what << ": picked kind " << int(picked.kind) << " " << picked.id.toString());
		CHECK(picked == c.expected);
	}

	// Looking away from the room, there is nothing to pick.
	CHECK(rigplan::pick(d.plan, d.layout, d.scene, rigplan::Ray{view.worldFromCamera.translation(), {0.0, -1.0, 0.0}}) == Selection::nothing());
}

TEST_CASE("a camera in front of a wall wins over the wall", "[rigplan][picking]")
{
	// A plane standing just behind cam1's body, square to it, and a click looking back at the camera from
	// in front of it: the camera is nearer.
	rp::RigPlan plan = editorPlan();
	const rp::NominalLayout layout = rp::nominalLayout(plan);
	const math::RigidTransformd cam1 = *layout.cameras[0].planFromCamera;
	const math::Vec3d forward = cam1.rotate({0.0, 0.0, 1.0});
	rp::Plane wall;
	wall.id = editorplan::id("e009");
	wall.name = "wall";
	wall.centre = cam1.translation() - forward * 0.07; // its body reaches 0.05 behind its optical centre
	wall.aim = rp::aimOf(cam1.rotation());
	plan.planes.push_back(wall);
	const rigplan::Ray look{cam1.translation() + forward * 2.0, -forward};

	const Drawn d = drawn(plan);
	CHECK(rigplan::pick(d.plan, d.layout, d.scene, look) == Selection::object(Selected::Camera, editorplan::cam1Id));

	// Without the camera, the same click is the wall's: so the wall is on the ray.
	rp::RigPlan bare = plan;
	bare.cameras.erase(bare.cameras.begin());
	const Drawn without = drawn(bare);
	CHECK(rigplan::pick(without.plan, without.layout, without.scene, look) == Selection::object(Selected::Plane, wall.id));
}

TEST_CASE("the ray through a pixel is the pinhole's", "[rigplan][picking]")
{
	rigplan::Orbit orbit;
	orbit.yawDegrees = 60;
	const lain::render::View view = rigplan::viewOf(orbit, kPane);
	const math::Vec3d point{1.5, -0.5, 2.0};
	const rigplan::Ray ray = rigplan::rayThrough(view, project(view, point));
	CHECK(math::length(ray.direction) == Catch::Approx(1.0));
	const math::Vec3d toPoint = point - ray.origin;
	CHECK(math::length(toPoint - ray.direction * math::dot(toPoint, ray.direction)) < 1e-9);
}
