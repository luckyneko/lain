// Integration: a rig plan's render scene drawn through a camera placed as a rig plan places one, so the
// plan frame, the camera frame and the renderer are held to one another. SKIPs where there is no device.

#include "gpu.h"

#include <lain/render/renderer.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lain;
using namespace lain::rigplan;

TEST_CASE("a camera at rest in a room sees its north wall ahead and its floor below", "[rigplan][scene][gpu]")
{
	acm::Device* device = render::testing::device();
	if (!device)
		SKIP("no Vulkan device on this machine");
	render::Renderer renderer(*device);
	REQUIRE(renderer.problems().empty());

	// An 8 m room with no ceiling: a red north wall, a green floor, and a bulb where the camera stands.
	RigPlan plan;
	Room room;
	room.size = {8.0, 8.0, 3.0};
	room.ceiling.present = false;
	room.north.material.surface = SolidColour{{1.0f, 0.0f, 0.0f}};
	room.floor.material.surface = SolidColour{{0.0f, 1.0f, 0.0f}};
	plan.room = room;
	LightSpecification bulb;
	bulb.name = "bulb";
	bulb.intensity = 4;
	plan.specifications.lights.push_back(bulb);
	Mount hook;
	hook.id = core::Uuid::generate();
	hook.name = "hook";
	hook.kind = FixedPoint{{0.0, 0.0, 1.5}};
	plan.mounts.push_back(hook);
	PlacedLight light;
	light.id = core::Uuid::generate();
	light.name = "bulb";
	light.light = "bulb";
	light.mount = hook.id;
	plan.lights.push_back(light);

	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const SceneResult scene = renderScene(plan, *realised.realisation, assets.assets, 0);
	REQUIRE(scene.problems.empty());
	render::UploadResult uploaded = renderer.upload(scene.scene);
	REQUIRE(uploaded.problems.empty());

	// A camera at rest on the same hook, as a rig plan places one: looking north, image down along -Z.
	render::View view;
	view.worldFromCamera = mountFromPlacement({0.0, 0.0, 1.5}, {});
	view.image = {64, 64};
	view.intrinsics = {32.0, 32.0, 31.5, 31.5}; // 90 degrees across
	const render::RenderResult drawn = renderer.render(*uploaded.scene, view);
	REQUIRE(drawn.problems.empty());
	auto at = [&](int x, int y)
	{
		const float* p = reinterpret_cast<const float*>(drawn.image.data()) + 4 * (y * 64 + x);
		return std::array<float, 4>{p[0], p[1], p[2], p[3]};
	};

	// Ahead, the wall 4 m away, lit face on by the bulb: 4 / 4^2.
	const std::array<float, 4> ahead = at(32, 32);
	CHECK(ahead[0] == Catch::Approx(0.25).epsilon(1e-3));
	CHECK(ahead[1] == 0.0f);
	// Below, the floor. Above, nothing: the ceiling is absent and the wall stops at 3 m.
	const std::array<float, 4> below = at(32, 60);
	CHECK(below[1] > 0.0f);
	CHECK(below[0] == 0.0f);
	CHECK(at(32, 2)[3] == 0.0f);
}
