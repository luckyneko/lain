// Unit: a rig plan's still scene (staticScene over a realisation) asked on the CPU what a camera's ray
// met (lain::rigplan::testing's cast, over lain::render's ray cast): where a feature a capture recorded
// came from, for the tests that render one. Each answer is judged against the room's own geometry or the
// realised pole's, never against another cast. The board's case is test_sceneboard.cpp's, which has a
// board renderer.

#include <lain/camera/projection.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>
#include <lain/rigplan/testing/scenecast.h>
#include <lain/rigplan/testing/testplan.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>

using namespace lain::rigplan;
using lain::rigplan::testing::cast;
using lain::rigplan::testing::SceneHit;
namespace math = lain::math;

// The test plan in a plain room 10 x 8 x 3.2 m about the origin: its north wall at y = 4, 0.875 m in
// front of cam0, which looks north from the pole at (2, 3).
static RigPlan inRoom()
{
	RigPlan plan = testplan::plan();
	Room room;
	room.size = {10.0, 8.0, 3.2};
	plan.room = room;
	return plan;
}

TEST_CASE("a camera's ray meets the wall it looks at, at the pixel's own point", "[rigplan][scene][cast]")
{
	const RigPlan plan = inRoom();
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const SceneResult still = staticScene(plan, *realised.realisation, assets.assets);
	REQUIRE(still.problems.empty());

	// Corners and centre of the image: the lens bends each ray, so each is held to its own pixel through
	// the realised model rather than to a pinhole's.
	const RealisedCamera& cam0 = realised.realisation->cameras[0];
	for (const math::Vec2d& pixel : {math::Vec2d{0.0, 0.0}, math::Vec2d{1279.0, 0.0}, math::Vec2d{0.0, 719.0}, math::Vec2d{1279.0, 719.0},
									 math::Vec2d{639.5, 359.5}, math::Vec2d{200.0, 600.0}})
	{
		INFO("pixel (" << pixel.x << ", " << pixel.y << ")");
		const std::optional<SceneHit> hit = cast(still, cam0, pixel);
		REQUIRE(hit);
		CHECK(hit->owner.object == SceneObject::Room);
		CHECK(hit->point.y == Catch::Approx(4.0).margin(1e-9)); // on the north wall's plane
		CHECK(hit->distance == Catch::Approx(math::distance(hit->point, cam0.planFromCamera.translation())).epsilon(1e-12));
		// The point that pixel shows: back through the camera's own model, it lands on the pixel.
		const math::Vec3d inCamera = cam0.planFromCamera.inverse().apply(hit->point);
		const lain::camera::Projection<double> back = lain::camera::project(cam0.model, inCamera.x, inCamera.y, inCamera.z);
		REQUIRE(back.ok());
		CHECK(std::hypot(back.u - pixel.x, back.v - pixel.y) < 1e-6);
	}
}

TEST_CASE("a pole stands in the still scene where its mount was realised, and hides what is behind it", "[rigplan][scene][cast]")
{
	const RigPlan plan = inRoom(); // the pole 5 cm across at (2, 3), with 10 mm of tolerance
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const SceneResult still = staticScene(plan, *realised.realisation, assets.assets);
	REQUIRE(still.problems.empty());

	// Its draw moved it: aimed along that move, a pole left where it was meant would be met that much
	// nearer or further.
	const math::Vec2d axis{realised.realisation->mounts[0].planFromMount.translation()};
	const math::Vec2d moved = axis - math::Vec2d{2.0, 3.0};
	REQUIRE(math::length(moved) > 2e-3);
	const math::Vec2d away = moved / math::length(moved);

	// From a metre off the realised axis, at 1 m up, toward it: the pole, not the room behind, its
	// surface within a 32-sided cylinder's sag (0.025 (1 - cos(pi / 32)) = 0.12 mm) of the radius.
	const lain::render::Ray toward{math::Vec3d{axis + away, 1.0}, math::Vec3d{-away, 0.0}};
	const std::optional<SceneHit> hit = cast(still, toward);
	REQUIRE(hit);
	CHECK(hit->owner.object == SceneObject::Pole);
	CHECK(hit->owner.id == testplan::poleId);
	CHECK(hit->distance == Catch::Approx(1.0 - 0.025).margin(2e-4));
}
