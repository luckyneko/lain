// What the Viewport shows of a plan's scene (M15 slice 8f): the dollhouse cut from the eye, what Frame
// All and Frame Selected frame, and the floor grid. Over the production render scene of the nominal
// layout, as the pane builds it.

#include "editorplan.h"
#include "selection.h"
#include "viewing.h"

#include <lain/core/uri.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace math = lain::math;
namespace rp = lain::rigplan;
using rigplan::Selected;
using rigplan::Selection;

// The editor's plan drawn at frame 0 from its geometry alone: the room, its plane and box, its pole.
struct Drawn
{
	rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	rp::NominalLayout layout;
	rp::SceneResult scene;

	Drawn()
	{
		layout = rp::nominalLayout(plan);
		rp::SceneAssets assets;
		rp::sceneGeometry(plan, assets);
		scene = rp::renderScene(plan, layout, assets, 0);
	}

	std::size_t roomSurfaces(const rp::SceneResult& s) const
	{
		std::size_t n = 0;
		for (const rp::SceneOwner& owner : s.owners)
			n += owner.object == rp::SceneObject::Room ? 1 : 0;
		return n;
	}
};

static bool inside(const rigplan::Bounds& b, const math::Vec3d& p)
{
	return p.x >= b.lower.x - 1e-9 && p.y >= b.lower.y - 1e-9 && p.z >= b.lower.z - 1e-9 && p.x <= b.upper.x + 1e-9 && p.y <= b.upper.y + 1e-9 &&
		   p.z <= b.upper.z + 1e-9;
}

TEST_CASE("the dollhouse leaves out each of the room's surfaces the eye is outside of, and nothing else", "[rigplan][viewing]")
{
	const Drawn d;
	REQUIRE(d.roomSurfaces(d.scene) == 6); // the New plan's room: 8 x 6 x 3 m about the origin
	const std::size_t others = d.scene.scene.instances.size() - 6;

	// From inside, every surface stays.
	CHECK(d.roomSurfaces(rigplan::cutaway(d.plan, d.scene, {0.0, 0.0, 1.5})) == 6);
	// From beyond the south wall (y < -3), at a person's height, that wall alone goes.
	const rp::SceneResult south = rigplan::cutaway(d.plan, d.scene, {0.0, -10.0, 1.5});
	CHECK(d.roomSurfaces(south) == 5);
	CHECK(south.scene.instances.size() == 5 + others); // the plane, the box and the pole stay
	CHECK(south.owners.size() == south.scene.instances.size());
	// From above and beyond a corner: the ceiling and the two walls on that side.
	CHECK(d.roomSurfaces(rigplan::cutaway(d.plan, d.scene, {10.0, -10.0, 10.0})) == 3);
	// From below the floor, the floor.
	CHECK(d.roomSurfaces(rigplan::cutaway(d.plan, d.scene, {0.0, 0.0, -2.0})) == 5);

	// The mask is the cutaway's: what changes it is what changes the scene.
	const std::vector<bool> mask = rigplan::cutawayMask(d.plan, d.scene, {0.0, -10.0, 1.5});
	CHECK(mask == rigplan::cutawayMask(d.plan, d.scene, {1.0, -9.0, 2.0})); // moved, crossing no wall
	CHECK(mask != rigplan::cutawayMask(d.plan, d.scene, {0.0, 10.0, 1.5}));

	// A plan without a room shows everything from anywhere.
	Drawn bare;
	bare.plan.room.reset();
	bare.layout = rp::nominalLayout(bare.plan);
	rp::SceneAssets assets;
	rp::sceneGeometry(bare.plan, assets);
	bare.scene = rp::renderScene(bare.plan, bare.layout, assets, 0);
	CHECK(rigplan::cutaway(bare.plan, bare.scene, {0.0, -10.0, 1.5}).scene.instances.size() == bare.scene.scene.instances.size());
}

TEST_CASE("Frame All holds everything, and Frame Selected the selection", "[rigplan][viewing]")
{
	const Drawn d;
	const std::optional<rigplan::Bounds> all = rigplan::boundsOfEverything(d.plan, d.layout, d.scene);
	REQUIRE(all);
	// The room's corners, and every camera, light and key.
	CHECK(inside(*all, {-4.0, -3.0, 0.0}));
	CHECK(inside(*all, {4.0, 3.0, 3.0}));
	for (const rp::NominalLayout::Camera& camera : d.layout.cameras)
		CHECK(inside(*all, camera.planFromCamera->translation()));
	for (const rp::PathKey& key : d.plan.boards.front().path.keys)
		CHECK(inside(*all, key.position));

	// A camera: its optical centre, half a metre across at least, and much less than the room.
	const std::optional<rigplan::Bounds> camera =
		rigplan::boundsOfSelection(d.plan, d.layout, d.scene, Selection::object(Selected::Camera, editorplan::cam1Id));
	REQUIRE(camera);
	CHECK(inside(*camera, d.layout.cameras[0].planFromCamera->translation()));
	CHECK(camera->upper.x - camera->lower.x >= 0.5 - 1e-12);
	CHECK(camera->upper.x - camera->lower.x < 1.0);

	// A mount: its pole, and the cameras on it.
	const std::optional<rigplan::Bounds> pole = rigplan::boundsOfSelection(d.plan, d.layout, d.scene, Selection::object(Selected::Mount, editorplan::poleId));
	REQUIRE(pole);
	CHECK(inside(*pole, {-2.0, 0.0, 0.0}));
	CHECK(inside(*pole, {-2.0, 0.0, 2.5}));
	CHECK(inside(*pole, d.layout.cameras[1].planFromCamera->translation()));
	CHECK_FALSE(inside(*pole, {4.0, 3.0, 3.0}));

	// A key, and its board: the key's position; the board's whole path.
	const std::optional<rigplan::Bounds> key = rigplan::boundsOfSelection(d.plan, d.layout, d.scene, Selection::keyOf(editorplan::boardId, 1));
	REQUIRE(key);
	CHECK(inside(*key, d.plan.boards.front().path.keys[1].position));
	const std::optional<rigplan::Bounds> board = rigplan::boundsOfSelection(d.plan, d.layout, d.scene, Selection::object(Selected::Board, editorplan::boardId));
	REQUIRE(board);
	for (const rp::PathKey& k : d.plan.boards.front().path.keys)
		CHECK(inside(*board, k.position));

	// The plan, a specification or nothing frame everything.
	for (const Selection& s : {Selection::plan(), Selection::nothing(), Selection::specificationOf(rigplan::SpecificationKind::Lens, "4mm")})
	{
		const std::optional<rigplan::Bounds> b = rigplan::boundsOfSelection(d.plan, d.layout, d.scene, s);
		REQUIRE(b);
		CHECK(b->lower == all->lower);
		CHECK(b->upper == all->upper);
	}
}

TEST_CASE("the floor grid covers the room's floor, or 20 m each way without one", "[rigplan][viewing]")
{
	rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	plan.room->centre = {1.0, -0.5};
	const lain::render::Grid grid = rigplan::floorGrid(plan);
	CHECK(lain::render::validate(grid).empty());
	CHECK(grid.origin == math::Vec3d{1.0, -0.5, 0.0});
	CHECK(grid.extent == math::Vec2d{4.0, 3.0});
	CHECK(grid.spacing == 0.1);
	CHECK(grid.majorEvery == 10);

	plan.room.reset();
	const lain::render::Grid open = rigplan::floorGrid(plan);
	CHECK(lain::render::validate(open).empty());
	CHECK(open.extent == math::Vec2d{20.0, 20.0});
}
