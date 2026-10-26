// A board mover in a rig plan's render scene, drawn by a stand-in board renderer: its own executable,
// since a renderer registry only grows and test_scene.cpp's must have none. The sheet's geometry is held
// to the board frame on the CPU, and its plain back on the device (SKIPs where there is none). And a
// moving board's frames share every other mesh and texture, so an update between them uploads none.

#include "gpu.h"
#include "testplan.h"

#include <lain/camera/board/rendering.h>
#include <lain/render/renderer.h>
#include <lain/rigplan/mover.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using namespace lain;
using namespace lain::rigplan;

// A board of the requested size, black squares inside a white margin, or nothing when asked to fail.
class StandInBoard : public camera::board::Renderer
{
public:
	static inline bool fail = false;

	image::Image raster(const camera::board::Pattern& pattern, const camera::board::RenderRequest& request) const override
	{
		if (fail)
			return {};
		const camera::board::PatternParameters& p = pattern.parameters();
		const int width = int(p.squaresX * request.pixelsPerSquare + 2 * request.marginPixels);
		const int height = int(p.squaresY * request.pixelsPerSquare + 2 * request.marginPixels);
		image::Image out(width, height, image::PixelFormat::Gray8, image::ColorSpace::sRGB);
		const int margin = int(request.marginPixels);
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const bool inside = x >= margin && x < width - margin && y >= margin && y < height - margin;
				out.data()[std::size_t(y) * std::size_t(width) + std::size_t(x)] = inside ? 0 : 255;
			}
		}
		return out;
	}
};

static void registerStandIn()
{
	static const bool once = []
	{
		camera::board::rendererRegistry().registerType<StandInBoard>("stand-in");
		return true;
	}();
	(void)once;
}

static RigPlan planWithBoard()
{
	RigPlan plan = testplan::plan();
	plan.boards.push_back(testplan::board());
	return plan;
}

TEST_CASE("a board's sheet spans its raster, with the squares where the board frame puts them", "[rigplan][scene][mover]")
{
	registerStandIn();
	const RigPlan plan = planWithBoard();
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const BoardSheet& sheet = assets.assets.boards.at(testplan::boardId);

	// 24 mm squares at 100 pixels each, and a 10 mm margin to the nearest pixel: 41.67, so 42.
	const double perMetre = 100 / 0.024;
	REQUIRE(sheet.raster->width() == 7 * 100 + 2 * 42);
	REQUIRE(sheet.raster->height() == 5 * 100 + 2 * 42);
	REQUIRE(sheet.raster->colorSpace() == image::ColorSpace::sRGB);

	// The print's corners are the raster's outer edges in the board frame, facing a camera in front.
	const mesh::Mesh& print = *sheet.print;
	REQUIRE(print.positions.size() == 4);
	const double x0 = -42 / perMetre, x1 = (784 - 42) / perMetre;
	const double y0 = -42 / perMetre, y1 = (584 - 42) / perMetre;
	CHECK(print.positions[0].x == Catch::Approx(x0).margin(1e-7));
	CHECK(print.positions[0].y == Catch::Approx(y0).margin(1e-7));
	CHECK(print.positions[2].x == Catch::Approx(x1).margin(1e-7));
	CHECK(print.positions[2].y == Catch::Approx(y1).margin(1e-7));
	CHECK(print.positions[0].z == 0.0f);
	CHECK(print.normals[0] == math::Vec3f{0.0f, 0.0f, -1.0f});

	// A corner of the squares, mapped through the print's coordinates, lands on the raster pixel boundary
	// that draws it: corner (i, j) of the squares at pixel (42 + 100 i, 42 + 100 j), with the raster's top
	// row at v = 1.
	const camera::board::Specification specification = *specificationOf(plan.boards[0]);
	for (std::uint32_t id = 0; id < specification.pattern().cornerCount(); ++id)
	{
		const math::Vec3d at = *specification.cornerPosition(id);
		const double u = (at.x - print.positions[0].x) / (print.positions[2].x - print.positions[0].x);
		const double v = (at.y - print.positions[0].y) / (print.positions[2].y - print.positions[0].y);
		const double expectedU = (42 + 100 * (at.x / 0.024)) / 784.0;
		const double expectedV = (42 + 100 * (at.y / 0.024)) / 584.0;
		REQUIRE(u == Catch::Approx(expectedU).margin(1e-6));
		REQUIRE(v == Catch::Approx(expectedV).margin(1e-6));
		REQUIRE(print.uvs[0] == math::Vec2f{0.0f, 1.0f}); // (x0, y0) is the raster's top-left
	}

	// The back is the same rectangle, plain, its backing behind the print and facing the other way.
	const mesh::Mesh& back = *sheet.back;
	CHECK(back.positions[0].z == Catch::Approx(0.003));
	CHECK(back.normals[0] == math::Vec3f{0.0f, 0.0f, 1.0f});
}

TEST_CASE("a board follows its path through the frames of a scene", "[rigplan][scene][mover]")
{
	registerStandIn();
	const RigPlan plan = planWithBoard();
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	for (std::size_t frame : {std::size_t{0}, std::size_t{6}, std::size_t{23}})
	{
		const SceneResult scene = renderScene(plan, *realised.realisation, assets.assets, frame);
		REQUIRE(scene.problems.empty());
		// The board's print and back, found by their meshes, are both placed by planFromBoard.
		const BoardSheet& sheet = assets.assets.boards.at(testplan::boardId);
		const render::Instance* printed = nullptr;
		const render::Instance* backed = nullptr;
		for (const render::Instance& instance : scene.scene.instances)
		{
			if (scene.scene.meshes[instance.mesh] == sheet.print)
				printed = &instance;
			if (scene.scene.meshes[instance.mesh] == sheet.back)
				backed = &instance;
		}
		REQUIRE(printed);
		REQUIRE(backed);
		const math::Mat4d expected = planFromBoard(plan.boards[0], frame).matrix();
		CHECK(printed->worldFromMesh == expected);
		CHECK(backed->worldFromMesh == expected);
		// The print shows the raster on white; the back is plain white.
		const render::Material& print = scene.scene.materials[printed->materials[0]];
		const render::Material& back = scene.scene.materials[backed->materials[0]];
		REQUIRE(print.texture.has_value());
		CHECK(scene.scene.textures[*print.texture] == sheet.raster);
		CHECK(print.diffuse == image::ColorRGBf{1.0f, 1.0f, 1.0f});
		CHECK_FALSE(back.texture.has_value());
		CHECK(back.diffuse == image::ColorRGBf{1.0f, 1.0f, 1.0f});
	}
}

TEST_CASE("a board the renderer will not draw is named", "[rigplan][scene][mover]")
{
	registerStandIn();
	StandInBoard::fail = true;
	const AssetsResult assets = loadSceneAssets(planWithBoard());
	StandInBoard::fail = false;
	REQUIRE(assets.problems.size() == 1);
	CHECK(assets.problems[0].find("board 'charuco'") != std::string::npos);
	CHECK(assets.assets.boards.empty());
}

TEST_CASE("a camera behind a board sees its plain back, not its pattern", "[rigplan][scene][mover][gpu]")
{
	registerStandIn();
	render::Renderer renderer(render::testing::device());
	REQUIRE(renderer.problems().empty());

	// A board whose squares are all black, at rest 2 m along +Y, between two bulbs.
	RigPlan plan;
	BoardMover board = testplan::board();
	board.path.keys = {{0, {0.0, 2.0, 1.5}, Aim{}}};
	plan.boards.push_back(board);
	LightSpecification bulb;
	bulb.name = "bulb";
	plan.specifications.lights.push_back(bulb);
	for (const double y : {1.0, 3.0})
	{
		Mount hook;
		hook.id = core::Uuid::generate();
		hook.name = "hook";
		hook.kind = FixedPoint{{0.0, y, 1.5}};
		plan.mounts.push_back(hook);
		PlacedLight light;
		light.id = core::Uuid::generate();
		light.name = "bulb";
		light.light = "bulb";
		light.mount = hook.id;
		plan.lights.push_back(light);
	}
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const SceneResult scene = renderScene(plan, *realised.realisation, assets.assets, 0);
	REQUIRE(scene.problems.empty());
	render::UploadResult uploaded = renderer.upload(scene.scene);
	REQUIRE(uploaded.problems.empty());

	// The squares' centre, seen from a metre in front (a camera at rest) and from a metre behind (turned
	// round). The renderer lights both sides of a surface, so only the back keeps the print out of view.
	const auto centreFrom = [&](double y, double pan)
	{
		render::View view;
		view.worldFromCamera = mountFromPlacement({0.0, y, 1.5}, Aim{pan, 0.0, 0.0});
		view.image = {33, 33};
		view.intrinsics = {200.0, 200.0, 16.0, 16.0};
		const render::RenderResult drawn = renderer.render(*uploaded.scene, view);
		REQUIRE(drawn.problems.empty());
		const float* p = reinterpret_cast<const float*>(drawn.image.data()) + 4 * (16 * 33 + 16);
		return p[0];
	};
	CHECK(centreFrom(1.0, 0.0) < 1e-3);	  // the black print
	CHECK(centreFrom(3.0, 180.0) > 0.5f); // the white back, a metre from its bulb
}

// The test plan in a room, with a procedural plane and the board moving across it: everything but the
// board stands still from one frame to the next.
static RigPlan movingInARoom()
{
	RigPlan plan = planWithBoard();
	Room room;
	room.size = {10.0, 8.0, 3.2};
	room.ceiling.present = false;
	plan.room = room;
	Plane patch;
	patch.id = testplan::partitionId;
	patch.name = "patch";
	patch.centre = {2.0, 5.0, 1.2};
	patch.size = {1.0, 1.0};
	patch.material.surface = image::ProceduralTexture{5, 0.05};
	plan.planes.push_back(patch);
	return plan;
}

TEST_CASE("two frames of a rig plan's scene share every mesh and texture", "[rigplan][scene][mover]")
{
	registerStandIn();
	const RigPlan plan = movingInARoom();
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const SceneResult first = renderScene(plan, *realised.realisation, assets.assets, 0);
	const SceneResult later = renderScene(plan, *realised.realisation, assets.assets, 6);
	REQUIRE(first.problems.empty());
	REQUIRE(later.problems.empty());
	REQUIRE(later.scene.meshes == first.scene.meshes);
	REQUIRE(later.scene.textures == first.scene.textures);
	// And what moved is only the board.
	const BoardSheet& sheet = assets.assets.boards.at(testplan::boardId);
	for (std::size_t i = 0; i < first.scene.instances.size(); ++i)
	{
		const std::shared_ptr<const mesh::Mesh>& mesh = first.scene.meshes[first.scene.instances[i].mesh];
		const bool board = mesh == sheet.print || mesh == sheet.back;
		CHECK((first.scene.instances[i].worldFromMesh == later.scene.instances[i].worldFromMesh) != board);
	}
}

TEST_CASE("an update from one frame to the next uploads nothing but what moved", "[rigplan][scene][mover][gpu]")
{
	registerStandIn();
	render::Renderer renderer(render::testing::device());
	REQUIRE(renderer.problems().empty());
	const RigPlan plan = movingInARoom();
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());

	render::UploadResult uploaded = renderer.upload(renderScene(plan, *realised.realisation, assets.assets, 0).scene);
	REQUIRE(uploaded.problems.empty());
	const render::Scene later = renderScene(plan, *realised.realisation, assets.assets, 6).scene;
	const render::UpdateResult updated = renderer.update(*uploaded.scene, later);
	REQUIRE(updated.problems.empty());
	CHECK(updated.meshesUploaded == 0);
	CHECK(updated.texturesUploaded == 0);

	// cam0 sees what a fresh upload of frame 6 shows it.
	render::View view;
	view.worldFromCamera = realised.realisation->cameras[0].planFromCamera;
	view.image = {64, 36};
	view.intrinsics = {50.0, 50.0, 31.5, 17.5};
	const render::RenderResult viaUpdate = renderer.render(*uploaded.scene, view);
	render::UploadResult fresh = renderer.upload(later);
	REQUIRE(fresh.problems.empty());
	const render::RenderResult viaUpload = renderer.render(*fresh.scene, view);
	REQUIRE(viaUpdate.problems.empty());
	REQUIRE(viaUpload.problems.empty());
	CHECK(std::memcmp(viaUpdate.image.data(), viaUpload.image.data(), viaUpload.image.byteSize()) == 0);
}
