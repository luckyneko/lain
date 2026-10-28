// Unit: a rig plan's assets as an editor keeps them (M15 sub-slice 8c). The work is jobs that run on any
// thread; what a plan lacks, and only that, is asked for; a failure is not retried while the plan asks
// for the same thing; a scene says which object of the plan each instance draws, and draws a surface
// waiting on its asset grey.

#include "testplan.h"

#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/io/mesh/codecs.h>
#include <lain/io/mesh/load.h>
#include <lain/rigplan/scene.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace lain;
using namespace lain::rigplan;
namespace fs = std::filesystem;

static void write(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

// A small sRGB picture of one grey.
static core::Uri picture(const fs::path& path, std::uint8_t grey)
{
	io::image::registerImageCodecs();
	if (!io::image::readerRegistry().contains("png"))
		SKIP("this build has no PNG codec");
	image::Image pixels(2, 2, image::PixelFormat::RGB8, image::ColorSpace::sRGB);
	std::fill(pixels.data(), pixels.data() + pixels.byteSize(), grey);
	fs::create_directories(path.parent_path());
	REQUIRE(io::image::save(core::Uri::fromPath(path), pixels));
	return core::Uri::fromPath(path);
}

// A one-triangle OBJ whose first vertex is at x = `first`, so which file was read shows.
static core::Uri triangle(const fs::path& path, int first)
{
	io::mesh::registerMeshCodecs();
	if (!io::mesh::readerRegistry().contains("obj"))
		SKIP("this build has no OBJ codec");
	write(path, "v " + std::to_string(first) + " 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
	return core::Uri::fromPath(path);
}

// The test plan in a room of procedural surfaces, with a plane and a box of the default flat colour:
// nothing to read from disk, and nothing but the room to bake.
static RigPlan furnishedInMemory()
{
	RigPlan plan = testplan::plan();
	Room room;
	room.size = {6.0, 5.0, 3.0};
	for (RoomSurface* surface : {&room.floor, &room.ceiling, &room.north, &room.south, &room.east, &room.west})
		surface->material.surface = image::ProceduralTexture{11, 0.25};
	plan.room = room;
	Plane partition;
	partition.id = testplan::partitionId;
	partition.name = "partition";
	partition.centre = {1.0, 2.0, 1.0};
	plan.planes.push_back(partition);
	Box crate;
	crate.id = testplan::crateId;
	crate.name = "crate";
	plan.boxes.push_back(crate);
	return plan;
}

static SceneResult nominalScene(const RigPlan& plan, const SceneAssets& assets)
{
	return renderScene(plan, nominalLayout(plan), assets, 0);
}

static bool samePixels(const image::Image& a, const image::Image& b)
{
	return a.width() == b.width() && a.height() == b.height() && a.pixelFormat() == b.pixelFormat() && a.byteSize() == b.byteSize() &&
		   std::memcmp(a.data(), b.data(), a.byteSize()) == 0;
}

TEST_CASE("every instance of a scene says which object of the plan it draws", "[rigplan][scene][assets]")
{
	const RigPlan plan = furnishedInMemory();
	const AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	const SceneResult scene = nominalScene(plan, assets.assets);
	REQUIRE(scene.problems.empty());
	REQUIRE(scene.owners.size() == scene.scene.instances.size());

	// The room's six surfaces, the plane, the box's six faces, then the pole, in renderScene's order.
	std::vector<SceneOwner> expected(6, SceneOwner{SceneObject::Room, core::Uuid{}});
	expected.push_back({SceneObject::Plane, testplan::partitionId});
	for (int face = 0; face < 6; ++face)
		expected.push_back({SceneObject::Box, testplan::crateId});
	expected.push_back({SceneObject::Pole, testplan::poleId});
	REQUIRE(scene.owners.size() == expected.size());
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		INFO("instance " << i);
		CHECK(scene.owners[i].object == expected[i].object);
		CHECK(scene.owners[i].id == expected[i].id);
	}
}

TEST_CASE("a plan's assets made on several threads in another order are the ones loadSceneAssets makes", "[rigplan][scene][assets]")
{
	const fs::path folder = testing::scratchPath("threaded");
	RigPlan plan = furnishedInMemory();
	plan.room->north.material.surface = ImageTexture{picture(folder / "north.png", 90), {1.0, 1.0}};
	plan.planes[0].material.surface = ImageTexture{picture(folder / "plane.png", 160), {1.0, 1.0}};
	Prop prop;
	prop.id = testplan::walkerId;
	prop.name = "stool";
	prop.mesh = triangle(folder / "stool.obj", 7);
	plan.props.push_back(prop);
	REQUIRE(check(plan).empty());

	const AssetsResult serial = loadSceneAssets(plan);
	REQUIRE(serial.problems.empty());

	SceneAssets threaded;
	sceneGeometry(plan, threaded);
	int rounds = 0;
	for (std::vector<AssetJob> jobs = missingAssets(plan, threaded); !jobs.empty(); jobs = missingAssets(plan, threaded))
	{
		std::vector<AssetOutcome> outcomes(jobs.size());
		std::vector<std::thread> threads;
		for (std::size_t i = 0; i < jobs.size(); ++i)
			threads.emplace_back([&, i]
								 { outcomes[i] = jobs[i].run(); });
		for (std::thread& thread : threads)
			thread.join();
		for (auto it = outcomes.rbegin(); it != outcomes.rend(); ++it) // kept in the other order
			addAsset(threaded, std::move(*it));
		REQUIRE(++rounds < 4);
	}

	CHECK(threaded.failed.empty());
	REQUIRE(threaded.baked.size() == serial.assets.baked.size());
	for (const auto& [key, bake] : serial.assets.baked)
	{
		REQUIRE(threaded.baked.count(key) == 1);
		CHECK(samePixels(*threaded.baked.at(key), *bake));
	}
	REQUIRE(threaded.pictures.size() == serial.assets.pictures.size());
	for (const auto& [uri, read] : serial.assets.pictures)
	{
		REQUIRE(threaded.pictures.count(uri) == 1);
		CHECK(samePixels(*threaded.pictures.at(uri), *read));
	}
	REQUIRE(threaded.props.count(prop.id) == 1);
	CHECK(threaded.props.at(prop.id).madeFrom == serial.assets.props.at(prop.id).madeFrom);
	CHECK(threaded.props.at(prop.id).clip->frames[0].positions == serial.assets.props.at(prop.id).clip->frames[0].positions);
}

TEST_CASE("after a room is resized, only its resized surfaces are baked again", "[rigplan][scene][assets]")
{
	RigPlan plan = furnishedInMemory();
	AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());
	REQUIRE(missingAssets(plan, assets.assets).empty());
	REQUIRE(assets.assets.baked.size() == 6); // the room's surfaces: the plane and the box are flat colours

	// Wider: the floor, the ceiling and the north and south walls change their extent; the east and west
	// walls do not.
	plan.room->size.x = 7.0;
	sceneGeometry(plan, assets.assets);
	const std::vector<AssetJob> jobs = missingAssets(plan, assets.assets);
	std::vector<std::string> what;
	for (const AssetJob& job : jobs)
		what.push_back(job.what);
	std::sort(what.begin(), what.end());
	CHECK(what == std::vector<std::string>{"baking the room's ceiling", "baking the room's floor", "baking the room's north wall",
										   "baking the room's south wall"});

	// While the four bake, those surfaces are waiting, and are drawn grey; the rest keep theirs.
	const SceneResult waiting = nominalScene(plan, assets.assets);
	CHECK(waiting.problems.size() == 4);
	pruneAssets(plan, assets.assets);
	CHECK(assets.assets.baked.size() == 2); // the old four go, unwanted, and the east and west walls stay
}

TEST_CASE("a surface waiting on its asset is drawn a matte mid grey, never white", "[rigplan][scene][assets]")
{
	const fs::path folder = testing::scratchPath("waiting");
	RigPlan plan = furnishedInMemory();
	plan.room->north.material.surface = ImageTexture{picture(folder / "north.png", 90), {1.0, 1.0}};

	SceneAssets nothing; // the geometry, and no job run yet
	sceneGeometry(plan, nothing);
	const SceneResult scene = nominalScene(plan, nothing);
	CHECK(scene.problems.size() == 6); // five bakes, and the picture
	CHECK(render::validate(scene.scene).empty());
	int awaited = 0;
	for (std::size_t i = 0; i < scene.scene.instances.size(); ++i)
	{
		if (scene.owners[i].object != SceneObject::Room)
			continue; // the plane and the box are flat colours, and a pole is grey whatever its assets hold
		const render::Material& material = scene.scene.materials[scene.scene.instances[i].materials[0]];
		INFO("instance " << i);
		CHECK_FALSE(material.texture.has_value());
		CHECK(material.diffuse.r == 0.5f);
		CHECK(material.diffuse.g == 0.5f);
		CHECK(material.diffuse.b == 0.5f);
		++awaited;
	}
	CHECK(awaited == 6);
}

TEST_CASE("a job that failed is not tried again until the plan stops asking for it", "[rigplan][scene][assets]")
{
	const fs::path folder = testing::scratchPath("failing");
	RigPlan plan = furnishedInMemory();
	const core::Uri absent = core::Uri::fromPath(folder / "absent.png");
	plan.room->north.material.surface = ImageTexture{absent, {1.0, 1.0}};

	AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.size() == 1);
	CHECK(assets.problems[0].find("north wall") != std::string::npos);
	CHECK(assets.assets.failed.size() == 1);
	CHECK(missingAssets(plan, assets.assets).empty()); // not asked for again

	// Another file: a new job. The old failure stays until the plan is pruned, which forgets it.
	const core::Uri elsewhere = core::Uri::fromPath(folder / "elsewhere.png");
	plan.room->north.material.surface = ImageTexture{elsewhere, {1.0, 1.0}};
	REQUIRE(missingAssets(plan, assets.assets).size() == 1);
	pruneAssets(plan, assets.assets);
	CHECK(assets.assets.failed.empty());

	// Back to the first file, which now exists: asked for again, and read.
	picture(folder / "absent.png", 40);
	plan.room->north.material.surface = ImageTexture{absent, {1.0, 1.0}};
	const std::vector<AssetJob> jobs = missingAssets(plan, assets.assets);
	REQUIRE(jobs.size() == 1);
	addAsset(assets.assets, jobs[0].run());
	CHECK(assets.assets.pictures.count(absent) == 1);
	CHECK(assets.assets.failed.empty());
}

TEST_CASE("a prop whose file changed is read again, and drawn as it was until it is", "[rigplan][scene][assets]")
{
	const fs::path folder = testing::scratchPath("reread");
	RigPlan plan = testplan::plan();
	Prop prop;
	prop.id = testplan::walkerId;
	prop.name = "stool";
	prop.up = UpAxis::Z;
	prop.mesh = triangle(folder / "first.obj", 3);
	plan.props.push_back(prop);
	AssetsResult assets = loadSceneAssets(plan);
	REQUIRE(assets.problems.empty());

	const auto firstVertex = [&](const SceneResult& scene)
	{
		for (std::size_t i = 0; i < scene.owners.size(); ++i)
		{
			if (scene.owners[i].object == SceneObject::Prop)
				return scene.scene.meshes[scene.scene.instances[i].mesh]->positions[0].x;
		}
		FAIL("no prop drawn");
		return 0.0f;
	};
	CHECK(firstVertex(nominalScene(plan, assets.assets)) == 3.0f);

	plan.props[0].mesh = triangle(folder / "second.obj", 5);
	const std::vector<AssetJob> jobs = missingAssets(plan, assets.assets);
	REQUIRE(jobs.size() == 1);
	CHECK(firstVertex(nominalScene(plan, assets.assets)) == 3.0f); // the mesh it had, while the new one is read
	addAsset(assets.assets, jobs[0].run());
	CHECK(firstVertex(nominalScene(plan, assets.assets)) == 5.0f);
	CHECK(missingAssets(plan, assets.assets).empty());

	// Moving it is not a change to its file.
	plan.props[0].position = {1.0, 1.0, 0.0};
	CHECK(missingAssets(plan, assets.assets).empty());
}

TEST_CASE("a plan's nominal scene leaves out a pole whose mount has no pose, and draws the rest", "[rigplan][scene][assets]")
{
	RigPlan plan = furnishedInMemory();
	std::get<Pole>(plan.mounts[0].kind).floor.x = std::numeric_limits<double>::quiet_NaN();
	AssetsResult assets = loadSceneAssets(plan);
	const SceneResult scene = nominalScene(plan, assets.assets);
	CHECK(scene.problems.empty()); // check() is what says why the pole is missing
	CHECK(render::validate(scene.scene).empty());
	CHECK(std::none_of(scene.owners.begin(), scene.owners.end(), [](const SceneOwner& o)
					   { return o.object == SceneObject::Pole; }));
	CHECK(scene.scene.instances.size() == 6 + 1 + 6);
	CHECK(scene.scene.lights.size() == 1); // the flood, on the wall bracket
}
