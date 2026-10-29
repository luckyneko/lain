// Unit: a rig plan's render scene, judged on the CPU: which surfaces, facing which way, their
// pictures laid out, props placed and played, lights posed. Files only where an asset must load.

#include "testplan.h"

#include <lain/image/procedural.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/io/mesh/codecs.h>
#include <lain/io/mesh/load.h>
#include <lain/render/renderer.h>
#include <lain/rigplan/scene.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace lain;
using namespace lain::rigplan;
namespace fs = std::filesystem;

static SceneResult sceneOf(const RigPlan& plan, const SceneAssets& assets, std::size_t frame = 0)
{
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	return renderScene(plan, *realised.realisation, assets, frame);
}

static SceneResult sceneOf(const RigPlan& plan)
{
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	return sceneOf(plan, assets.assets);
}

static RigPlan inRoom(const Room& room)
{
	RigPlan plan = testplan::plan();
	plan.room = room;
	return plan;
}

static const mesh::Mesh& meshOf(const render::Scene& scene, std::size_t instance)
{
	return *scene.meshes[scene.instances[instance].mesh];
}

static math::Vec3d normalOf(const render::Scene& scene, std::size_t instance)
{
	return math::Vec3d{meshOf(scene, instance).normals[0]};
}

static math::Vec3d centreOf(const render::Scene& scene, std::size_t instance)
{
	math::Vec3d sum{0.0};
	for (const math::Vec3f& p : meshOf(scene, instance).positions)
		sum += math::Vec3d{p};
	return sum / 4.0;
}

// The corner of a surface's mesh nearest `point`, and its texture coordinate.
static math::Vec2f uvNear(const mesh::Mesh& mesh, const math::Vec3d& point)
{
	std::size_t nearest = 0;
	for (std::size_t v = 1; v < mesh.positions.size(); ++v)
	{
		if (math::distance(math::Vec3d{mesh.positions[v]}, point) < math::distance(math::Vec3d{mesh.positions[nearest]}, point))
			nearest = v;
	}
	REQUIRE(math::distance(math::Vec3d{mesh.positions[nearest]}, point) < 1e-5);
	return mesh.uvs[nearest];
}

static void write(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

// A small picture that states no colour space, as most PNGs do.
static core::Uri untaggedPicture(const fs::path& path)
{
	io::image::registerImageCodecs();
	if (!io::image::readerRegistry().contains("png"))
		SKIP("this build has no PNG codec");
	image::Image picture(2, 2, image::PixelFormat::RGB8);
	std::fill(picture.data(), picture.data() + picture.byteSize(), std::uint8_t{200});
	fs::create_directories(path.parent_path());
	REQUIRE(io::image::save(core::Uri::fromPath(path), picture));
	return core::Uri::fromPath(path);
}

TEST_CASE("a room's present surfaces are drawn, each facing in", "[rigplan][scene]")
{
	Room room;
	room.centre = {1.0, -2.0};
	room.size = {10.0, 8.0, 3.2};
	room.ceiling.present = false;
	const SceneResult result = sceneOf(inRoom(room));
	REQUIRE(result.problems.empty());
	const render::Scene& scene = result.scene;
	CHECK(render::validate(scene).empty());
	REQUIRE(scene.instances.size() == 6); // and the test plan's pole, last

	const math::Vec3d middle{1.0, -2.0, 1.6};
	for (std::size_t i = 0; i < 5; ++i)
		CHECK(math::dot(normalOf(scene, i), middle - centreOf(scene, i)) > 0);

	// The floor first, at Z = 0 over the room's extent, then the walls, the north one at +Y.
	for (const math::Vec3f& p : meshOf(scene, 0).positions)
	{
		CHECK(p.z == 0.0f);
		CHECK((p.x == -4.0f || p.x == 6.0f));
		CHECK((p.y == -6.0f || p.y == 2.0f));
	}
	CHECK(centreOf(scene, 1).y == Catch::Approx(2.0));
	CHECK(normalOf(scene, 1).y == Catch::Approx(-1.0));
}

TEST_CASE("a picture repeats every tile and reads upright from the side it faces", "[rigplan][scene]")
{
	const core::Uri brick = untaggedPicture(testing::scratchPath("brick", ".png"));
	Room room;
	room.size = {10.0, 8.0, 3.2};
	room.north.material.surface = ImageTexture{brick, {0.5, 0.25}};
	const RigPlan plan = inRoom(room);

	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	REQUIRE(assets.assets.pictures.count(brick) == 1);
	CHECK(assets.assets.pictures.at(brick)->colorSpace() == image::ColorSpace::sRGB);	 // taken as sRGB
	CHECK(assets.assets.pictures.at(brick)->pixelFormat() == image::PixelFormat::RGBA8); // and stored

	const SceneResult result = sceneOf(plan, assets.assets);
	REQUIRE(result.problems.empty());
	const render::Scene& scene = result.scene;
	// The north wall, seen from inside facing north: +X to the right, +Z up, one picture a tile.
	const mesh::Mesh& wall = meshOf(scene, 2);
	REQUIRE(normalOf(scene, 2).y == Catch::Approx(-1.0));
	const math::Vec2f bottomLeft = uvNear(wall, {-5.0, 4.0, 0.0});
	const math::Vec2f topRight = uvNear(wall, {5.0, 4.0, 3.2});
	CHECK(bottomLeft.x == 0.0f);
	CHECK(bottomLeft.y == 0.0f);
	CHECK(topRight.x == Catch::Approx(10.0 / 0.5));
	CHECK(topRight.y == Catch::Approx(3.2 / 0.25));
	const render::Material& material = scene.materials[scene.instances[2].materials[0]];
	REQUIRE(material.texture);
	CHECK(scene.textures[*material.texture].get() == assets.assets.pictures.at(brick).get());
}

TEST_CASE("a procedural surface reads proceduralGrey in metres from its corner", "[rigplan][scene]")
{
	Room room;
	room.centre = {0.5, 0.25};
	room.size = {2.0, 1.5, 2.5};
	room.floor.material.surface = image::ProceduralTexture{11, 0.05};
	const RigPlan plan = inRoom(room);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	REQUIRE(assets.assets.baked.size() == 1);
	const auto& [key, bake] = *assets.assets.baked.begin();
	CHECK(key.texelsPerMetre == Catch::Approx(kBakedTexelsPerCell / 0.05)); // 400: small enough to be uncapped
	CHECK(key.width == 800);
	CHECK(key.height == 600);
	CHECK(key.seed != 11); // mixed with the surface's identity
	// The texture under the mixed seed, at that density and size, held as the device stores it. Where a
	// texel samples (its centre) is lain::image's, and its test pins it.
	const image::Image expected = render::storedTexture(image::bakeProceduralTexture({key.seed, 0.05}, 800, 600, 400));
	REQUIRE(bake->pixelFormat() == image::PixelFormat::RGBA8);
	REQUIRE(bake->byteSize() == expected.byteSize());
	CHECK(std::memcmp(bake->data(), expected.data(), expected.byteSize()) == 0);

	// The floor's corner at its south-west is texel (0, 0); its north-east corner the bake's far edge.
	// The renderer reads v upward, so texel row t * d is at v = 1 - t * d / height.
	const SceneResult result = sceneOf(plan, assets.assets);
	REQUIRE(result.problems.empty());
	const mesh::Mesh& floor = meshOf(result.scene, 0);
	const math::Vec2f origin = uvNear(floor, {-0.5, -0.5, 0.0});
	const math::Vec2f far = uvNear(floor, {1.5, 1.0, 0.0});
	CHECK(origin.x == 0.0f);
	CHECK(origin.y == 1.0f);
	CHECK(far.x == Catch::Approx(1.0));
	CHECK(far.y == Catch::Approx(0.0).margin(1e-6));
	const render::Material& material = result.scene.materials[result.scene.instances[0].materials[0]];
	REQUIRE(material.texture);
	CHECK(result.scene.textures[*material.texture].get() == bake.get());
}

TEST_CASE("no two surfaces of one procedural material repeat each other", "[rigplan][scene]")
{
	Room room;
	room.size = {3.0, 3.0, 2.5};
	const image::ProceduralTexture shared{11, 0.1};
	for (RoomSurface* surface : {&room.floor, &room.ceiling, &room.north, &room.south, &room.east, &room.west})
		surface->material.surface = shared;
	const RigPlan plan = inRoom(room);

	const AssetsResult first = loadSceneAssets(plan);
	REQUIRE(first.assets.baked.size() == 6);
	std::set<std::uint64_t> seeds;
	std::set<std::uint8_t> corners; // each bake's first texel, its stored code
	for (const auto& [key, bake] : first.assets.baked)
	{
		seeds.insert(key.seed);
		corners.insert(bake->data()[0]);
	}
	CHECK(seeds.size() == 6);
	CHECK(corners.size() > 1);

	// And the same every time the plan is loaded.
	const AssetsResult second = loadSceneAssets(plan);
	REQUIRE(second.assets.baked.size() == 6);
	CHECK(std::equal(first.assets.baked.begin(), first.assets.baked.end(), second.assets.baked.begin(),
					 [](const auto& a, const auto& b)
					 { return a.first.seed == b.first.seed; }));
}

TEST_CASE("a plane faces as a camera aimed the same way looks, and a box's faces face out", "[rigplan][scene]")
{
	RigPlan plan = testplan::plan();
	Plane plane;
	plane.id = core::Uuid::generate();
	plane.name = "partition";
	plane.centre = {1.0, 2.0, 1.0};
	plane.size = {2.0, 1.5};
	plane.aim = {90.0, 0.0, 0.0}; // panned a quarter turn: a camera so aimed looks along -X
	plan.planes.push_back(plane);
	Box box;
	box.id = core::Uuid::generate();
	box.name = "crate";
	box.centre = {-2.0, 1.0, 0.4};
	box.size = {0.8, 0.6, 0.4};
	box.yawDegrees = 30;
	plan.boxes.push_back(box);

	const SceneResult result = sceneOf(plan);
	REQUIRE(result.problems.empty());
	const render::Scene& scene = result.scene;
	REQUIRE(scene.instances.size() == 8); // and the test plan's pole, last

	// The plane faces the camera that would see it face on: one panned the same way looks along -X,
	// so the plane faces +X, upright, and spans its size across Y and up Z.
	CHECK(normalOf(scene, 0).x == Catch::Approx(1.0));
	CHECK(centreOf(scene, 0).x == Catch::Approx(1.0));
	for (const math::Vec3f& p : meshOf(scene, 0).positions)
	{
		CHECK(std::abs(p.y - 2.0f) == Catch::Approx(1.0));
		CHECK(std::abs(p.z - 1.0f) == Catch::Approx(0.75));
	}

	// Each face of the box lies half the box's size out along its own normal.
	const math::Quatd yaw = math::angleAxis(math::radians(30.0), math::Vec3d{0.0, 0.0, 1.0});
	const math::Vec3d half = box.size / 2.0;
	for (std::size_t face = 1; face < 7; ++face)
	{
		const math::Vec3d n = normalOf(scene, face);
		const math::Vec3d out = centreOf(scene, face) - box.centre;
		CHECK(math::length(math::cross(n, out)) == Catch::Approx(0.0).margin(1e-6)); // along its normal
		CHECK(math::dot(n, out) > 0);												 // and outward
		const math::Vec3d local = math::conjugate(yaw) * n;
		CHECK(math::dot(n, out) == Catch::Approx(std::abs(local.x) * half.x + std::abs(local.y) * half.y + std::abs(local.z) * half.z));
	}
}

TEST_CASE("a prop is placed by planFromProp, plays its clip, and shows its file's materials", "[rigplan][scene]")
{
	io::mesh::registerMeshCodecs();
	if (!io::mesh::readerRegistry().contains("obj"))
		SKIP("this build has no OBJ codec");
	const fs::path folder = testing::scratchPath("walker");
	const core::Uri skin = untaggedPicture(folder / "skin.png");
	write(folder / "walk.mtl", "newmtl skin\nKd 0.25 0.5 0.75\nKs 0.1 0.1 0.1\nNs 8\nmap_Kd skin.png\n");
	// Three frames, each a triangle whose first vertex says which frame it is.
	for (int f = 0; f < 3; ++f)
		write(folder / ("walk.000" + std::to_string(f + 1) + ".obj"),
			  "mtllib walk.mtl\nv " + std::to_string(f) + " 0 0\nv 1 0 0\nv 0 1 0\nusemtl skin\nf 1 2 3\n");

	RigPlan plan = testplan::plan();
	plan.timeline.rate = {24, 1};
	Prop walker;
	walker.id = testplan::walkerId;
	walker.name = "walker";
	walker.mesh = core::Uri::fromPath(folder / "walk.<frame:04>.obj");
	walker.clip = true;
	walker.clipRate = {12, 1}; // half the capture's rate
	walker.unitMetres = 0.01;
	walker.position = {0.5, -1.0, 0.0};
	walker.yawDegrees = 20;
	plan.props.push_back(walker);

	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	REQUIRE(assets.assets.props.at(walker.id).clip->frames.size() == 3);

	// At half the capture's rate, capture frames 0 to 5 show clip frames 0, 1, 1, 2, 2, 0 (nearest,
	// halves up, looping).
	const int expected[6] = {0, 1, 1, 2, 2, 0};
	for (std::size_t frame = 0; frame < 6; ++frame)
	{
		const SceneResult result = renderScene(plan, *realised.realisation, assets.assets, frame);
		REQUIRE(result.problems.empty());
		const render::Scene& scene = result.scene;
		REQUIRE(scene.instances.size() == 2); // and the test plan's pole, after it
		CHECK(scene.meshes[0]->positions[0].x == float(expected[frame]));
		// The clip's own frame, shared rather than copied.
		CHECK(scene.meshes[0].get() == &assets.assets.props.at(walker.id).clip->frames[std::size_t(expected[frame])]);
		CHECK(scene.instances[0].worldFromMesh == planFromProp(walker, 0));
	}

	const SceneResult result = renderScene(plan, *realised.realisation, assets.assets, 0);
	const render::Material& material = result.scene.materials[result.scene.instances[0].materials[0]];
	CHECK(material.diffuse.r == 0.25f);
	CHECK(material.diffuse.b == 0.75f);
	CHECK(material.specular.g == Catch::Approx(0.1f));
	CHECK(material.shininess == 8.0f);
	REQUIRE(material.texture);
	CHECK(result.scene.textures[*material.texture].get() == assets.assets.pictures.at(skin).get());
	CHECK(render::validate(result.scene).empty());
}

TEST_CASE("a pole is drawn from the floor to its top where its mount is realised", "[rigplan][scene]")
{
	const RigPlan plan = testplan::plan(); // a pole 2.5 m high and 5 cm across, with 10 mm of tolerance
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const SceneResult result = sceneOf(plan);
	REQUIRE(result.problems.empty());
	const render::Scene& scene = result.scene;
	CHECK(render::validate(scene).empty());
	REQUIRE(scene.instances.size() == 1);

	const math::Vec3d axis = realised.realisation->mounts[0].planFromMount.translation();
	CHECK(math::distance(math::Vec2d{axis}, math::Vec2d{2.0, 3.0}) > 0); // moved by its draw
	double lowest = 1e9, highest = -1e9, widest = 0;
	for (const math::Vec3f& p : meshOf(scene, 0).positions)
	{
		const math::Vec3d at{scene.instances[0].worldFromMesh * math::Vec4d{math::Vec3d{p}, 1.0}};
		lowest = std::min(lowest, at.z);
		highest = std::max(highest, at.z);
		widest = std::max(widest, math::distance(math::Vec2d{at}, math::Vec2d{axis}));
	}
	CHECK(lowest == Catch::Approx(0.0).margin(1e-6));
	CHECK(highest == Catch::Approx(2.5));
	CHECK(widest == Catch::Approx(0.025).epsilon(1e-6));
	CHECK(scene.materials[scene.instances[0].materials[0]].diffuse.r == 0.5f);
}

TEST_CASE("a light is its specification at its realised pose", "[rigplan][scene]")
{
	RigPlan plan = testplan::plan();
	plan.specifications.lights[0].intensity = 3;
	plan.specifications.lights[0].colour = {1.0f, 0.9f, 0.8f};
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const SceneResult result = sceneOf(plan);
	REQUIRE(result.problems.empty());
	REQUIRE(result.scene.lights.size() == 1);

	const render::Light& light = result.scene.lights[0];
	const math::RigidTransformd& pose = realised.realisation->lights[0].planFromLight;
	CHECK(light.kind == render::LightKind::Spot);
	CHECK(light.position == pose.translation());
	CHECK(light.direction == pose.rotate({0.0, 0.0, 1.0}));
	CHECK(light.direction.z < 0); // the flood is tilted down 45 degrees
	CHECK(light.intensity == 3);
	CHECK(light.colour.g == 0.9f);
	CHECK(light.innerConeDegrees == plan.specifications.lights[0].innerConeDegrees);
	CHECK(light.outerConeDegrees == plan.specifications.lights[0].outerConeDegrees);
}

TEST_CASE("what will not load is a problem naming its owner and its file", "[rigplan][scene]")
{
	RigPlan plan = testplan::furnished(testing::scratchPath("absent"));
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.size() == 2); // the north wall's picture, and the walker's clip
	CHECK(assets.problems[0].find("north wall") != std::string::npos);
	CHECK(assets.problems[0].find("brick.png") != std::string::npos);
	CHECK(assets.problems[1].find("walker") != std::string::npos);
	CHECK(assets.assets.baked.size() == 1); // the floor still bakes

	// Drawn from what did load, the missing pieces are named again.
	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const SceneResult result = renderScene(plan, *realised.realisation, assets.assets, 0);
	CHECK(result.problems.size() == 2);
	CHECK(render::validate(result.scene).empty()); // what is there can still be drawn
}

TEST_CASE("a board needs a board renderer, and without one its load names it", "[rigplan][scene][mover]")
{
	// This executable registers no board renderer, as a build without the OpenCV plugin has none. The rig
	// plan still holds its board; only drawing it is refused, and the rest of the scene still loads.
	RigPlan plan = testplan::plan();
	plan.boards.push_back(testplan::board());
	REQUIRE(check(plan).empty());
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.size() == 1);
	CHECK(assets.problems[0].find("board 'charuco'") != std::string::npos);
	CHECK(assets.problems[0].find("no board renderer") != std::string::npos);
	CHECK(assets.assets.boards.empty());

	const RealisationResult realised = realise(plan);
	REQUIRE(realised.realisation);
	const SceneResult result = renderScene(plan, *realised.realisation, assets.assets, 0);
	REQUIRE(result.problems.size() == 1);
	CHECK(result.problems[0].find("board 'charuco'") != std::string::npos);
	CHECK(render::validate(result.scene).empty());
}
