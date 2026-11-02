// How the visibility pane makes and shows its map, without a device: the schedule that hands out each
// face once and counts each camera on the pool, held during a gesture and restarted by a new revision,
// and the picture, its legend and the cell under the pointer. Faces are "drawn" here as distances
// filled by hand: kNoSurface (nothing in the way, so every cell in an image is seen) or 0 (nothing seen).

#include "testcameras.h"
#include "visibilityschedule.h"
#include "visibilityview.h"

#include <lain/render/renderer.h>
#include <lain/rigplan/visibility.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <set>
#include <thread>
#include <utility>
#include <vector>

using namespace lain;
namespace rp = lain::rigplan;
using ::rigplan::VisibilityFaceJob;
using ::rigplan::VisibilitySchedule;

static constexpr std::uint32_t kFace = 16; // texels a side: enough for distances that are one value

// A camera of strong camera `index` at `eye`, looking straight down.
static rp::VisibilityCamera downward(int index, const math::Vec3d& eye, const char* name)
{
	const math::Mat3d rotation{math::Vec3d{1.0, 0.0, 0.0}, math::Vec3d{0.0, -1.0, 0.0}, math::Vec3d{0.0, 0.0, -1.0}};
	return {core::Uuid::generate(), name, *camera::CameraModel::create(render::testing::strongCamera(index)).model, {math::quat_cast(rotation), eye}};
}

static rp::VisibilityGrid floorGrid()
{
	rp::VisibilityGrid grid;
	grid.origin = {-2.0, -2.0};
	grid.cell = 0.25;
	grid.columns = 16;
	grid.rows = 16;
	return grid;
}

static render::DistanceResult filled(float value)
{
	image::Image distances(int(kFace), int(kFace), image::PixelFormat::Gray32F);
	float* d = reinterpret_cast<float*>(distances.data());
	for (std::uint32_t i = 0; i < kFace * kFace; ++i)
		d[i] = value;
	return {std::move(distances), {}};
}

static std::function<VisibilitySchedule::Request()> requestOf(std::vector<rp::VisibilityCamera> cameras)
{
	return [cameras = std::move(cameras)]
	{ return VisibilitySchedule::Request{floorGrid(), cameras}; };
}

static std::vector<rp::VisibilityCamera> threeCameras()
{
	return {downward(0, {-1.0, 0.0, 2.0}, "left"), downward(5, {0.0, 0.0, 1.5}, "fisheye"), downward(1, {1.0, 0.0, 2.0}, "right")};
}

// Draws every face the schedule hands out with `value`, collecting counts as it goes, until it has
// nothing more to hand out and nothing left counting. Bounded, so a schedule that never finishes fails.
static std::vector<std::pair<std::size_t, std::size_t>> drawAll(VisibilitySchedule& schedule, float value)
{
	std::vector<std::pair<std::size_t, std::size_t>> handed;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (schedule.working() && std::chrono::steady_clock::now() < deadline)
	{
		while (std::optional<VisibilityFaceJob> job = schedule.next())
		{
			handed.push_back({job->camera, job->face});
			schedule.deliver(*job, filled(value));
		}
		schedule.poll();
		std::this_thread::yield();
	}
	REQUIRE_FALSE(schedule.working());
	return handed;
}

TEST_CASE("every face a map needs is handed out once, and every camera is counted", "[rigplan][visibility]")
{
	std::optional<testing::ThreadPool> pool;
	SECTION("inline") {}
	SECTION("on the process pool") { pool.emplace(3); }

	const std::vector<rp::VisibilityCamera> cameras = threeCameras();
	VisibilitySchedule schedule(VisibilitySchedule::kHeldCameras, kFace);
	schedule.want(1, false, true, requestOf(cameras));
	REQUIRE(schedule.working());
	const auto handed = drawAll(schedule, render::kNoSurface);

	std::size_t faces = 0;
	for (const rp::VisibilityCamera& camera : cameras)
		faces += rp::visibilityFaces(camera.model, kFace).size();
	// A pinhole's front, the fisheye's five, and the strong barrel lens's three: its corners reach past
	// 45 degrees, onto the side faces.
	CHECK(rp::visibilityFaces(cameras[2].model, kFace).size() == 3);
	CHECK(faces == 9);
	CHECK(handed.size() == faces);
	CHECK(std::set<std::pair<std::size_t, std::size_t>>(handed.begin(), handed.end()).size() == faces);

	// The map is what seenBy gives each camera over the same distances.
	const rp::VisibilityMap& map = schedule.map();
	REQUIRE(map.cameras().size() == 3);
	CHECK_FALSE(schedule.outOfDate());
	rp::VisibilityMap expected(floorGrid());
	for (const rp::VisibilityCamera& camera : cameras)
	{
		std::vector<rp::FaceDistances> drawn;
		for (const render::CaptureFace& face : rp::visibilityFaces(camera.model, kFace))
			drawn.push_back({face, filled(render::kNoSurface).image});
		expected.add(camera.id, rp::seenBy(floorGrid(), camera, drawn));
	}
	int differ = 0, seen = 0;
	for (int row = 0; row < 16; ++row)
	{
		for (int column = 0; column < 16; ++column)
		{
			differ += map.count(column, row) != expected.count(column, row) ? 1 : 0;
			seen += map.count(column, row) > 0 ? 1 : 0;
		}
	}
	CHECK(seen > 50);
	CHECK(differ == 0);
}

TEST_CASE("a new revision starts again, and a face drawn for the map it abandoned is not kept", "[rigplan][visibility]")
{
	VisibilitySchedule schedule(VisibilitySchedule::kHeldCameras, kFace);
	schedule.want(1, false, true, requestOf(threeCameras()));
	const std::optional<VisibilityFaceJob> stale = schedule.next();
	REQUIRE(stale);

	const std::vector<rp::VisibilityCamera> one{downward(0, {0.0, 0.0, 2.0}, "only")};
	schedule.want(2, false, true, requestOf(one));
	REQUIRE(schedule.revision() == 2);
	const std::optional<VisibilityFaceJob> fresh = schedule.next();
	REQUIRE(fresh);
	CHECK(fresh->revision == 2);
	CHECK(fresh->camera == 0);
	CHECK(fresh->face == 0);

	// The abandoned map's face, drawn as seeing nothing, comes back first: it must not stand for this one's.
	schedule.deliver(*stale, filled(0.0f));
	schedule.deliver(*fresh, filled(render::kNoSurface));
	drawAll(schedule, render::kNoSurface);
	REQUIRE(schedule.map().cameras() == std::vector<core::Uuid>{one[0].id});
	int seen = 0;
	for (int row = 0; row < 16; ++row)
	{
		for (int column = 0; column < 16; ++column)
			seen += schedule.map().count(column, row) > 0 ? 1 : 0;
	}
	CHECK(seen > 20);
}

TEST_CASE("at most so many cameras' faces are held, until their counts are collected", "[rigplan][visibility]")
{
	std::vector<rp::VisibilityCamera> four;
	for (int i = 0; i < 4; ++i)
		four.push_back(downward(0, {i - 1.5, 0.0, 2.0}, "cam"));
	VisibilitySchedule schedule(2, kFace);
	schedule.want(1, false, true, requestOf(four));

	// The pool is not started, so a count is done as soon as it is posted; still, its camera's faces are
	// held until poll() collects it.
	std::vector<std::size_t> cameras;
	while (std::optional<VisibilityFaceJob> job = schedule.next())
	{
		cameras.push_back(job->camera);
		schedule.deliver(*job, filled(render::kNoSurface));
	}
	CHECK(cameras == std::vector<std::size_t>{0, 1});
	schedule.poll();
	while (std::optional<VisibilityFaceJob> job = schedule.next())
	{
		cameras.push_back(job->camera);
		schedule.deliver(*job, filled(render::kNoSurface));
	}
	CHECK(cameras == std::vector<std::size_t>{0, 1, 2, 3});
	schedule.poll();
	CHECK_FALSE(schedule.working());
	CHECK(schedule.map().cameras().size() == 4);
}

TEST_CASE("a map is not started during a gesture or while its scene waits on a mesh, and the last stays", "[rigplan][visibility]")
{
	VisibilitySchedule schedule(VisibilitySchedule::kHeldCameras, kFace);
	schedule.want(1, false, true, requestOf(threeCameras()));
	drawAll(schedule, render::kNoSurface);
	const std::uint64_t shown = schedule.shownVersion();

	schedule.want(2, true, true, requestOf(threeCameras())); // a drag
	CHECK_FALSE(schedule.working());
	CHECK(schedule.revision() == 1);
	CHECK(schedule.outOfDate());
	CHECK_FALSE(schedule.waiting());

	schedule.want(2, false, false, requestOf(threeCameras())); // the drag ended; a prop's mesh is loading
	CHECK_FALSE(schedule.working());
	CHECK(schedule.waiting());
	CHECK(schedule.outOfDate());
	CHECK(schedule.shownVersion() == shown);
	CHECK(schedule.map().cameras().size() == 3);

	schedule.want(2, false, true, requestOf(threeCameras()));
	CHECK(schedule.working());
	CHECK_FALSE(schedule.waiting());
	drawAll(schedule, render::kNoSurface);
	CHECK_FALSE(schedule.outOfDate());
	CHECK(schedule.revision() == 2);
}

TEST_CASE("the map shown is the last one completed, and a remade one replaces it whole", "[rigplan][visibility]")
{
	VisibilitySchedule schedule(VisibilitySchedule::kHeldCameras, kFace);
	schedule.want(1, false, true, requestOf(threeCameras()));
	drawAll(schedule, render::kNoSurface);
	const std::uint64_t first = schedule.shownVersion();
	const std::size_t seenFirst = schedule.map().count(8, 8);
	REQUIRE(seenFirst > 0);

	// Remade with faces that see nothing: until it is complete, the first map is shown.
	schedule.want(2, false, true, requestOf(threeCameras()));
	for (int k = 0; k < 2; ++k)
	{
		const std::optional<VisibilityFaceJob> job = schedule.next();
		REQUIRE(job);
		schedule.deliver(*job, filled(0.0f));
		schedule.poll();
	}
	CHECK(schedule.working());
	CHECK(schedule.shownVersion() == first);
	CHECK(schedule.map().count(8, 8) == seenFirst);

	// An edit undone back to the map shown abandons the one under way.
	schedule.want(1, false, true, requestOf(threeCameras()));
	CHECK_FALSE(schedule.working());
	CHECK_FALSE(schedule.outOfDate());
	CHECK(schedule.map().count(8, 8) == seenFirst);

	schedule.want(3, false, true, requestOf(threeCameras()));
	drawAll(schedule, 0.0f);
	CHECK(schedule.shownVersion() != first);
	CHECK(schedule.map().count(8, 8) == 0);
}

TEST_CASE("the map's picture is a floor plan in the legend's colours, and the pointer finds its cell", "[rigplan][visibility]")
{
	const std::vector<::rigplan::LegendEntry>& legend = ::rigplan::visibilityLegend();
	REQUIRE(legend.size() == 7);
	for (std::size_t i = 0; i < legend.size(); ++i)
	{
		CHECK(legend[i].count == i);
		CHECK(legend[i].orMore == (i == 6));
	}
	CHECK(::rigplan::visibilityColour(9) == legend[6].colour);

	// Three cameras of a 3 x 2 grid: cell (c, r) seen by c + r of them.
	rp::VisibilityGrid grid;
	grid.columns = 3;
	grid.rows = 2;
	rp::VisibilityMap map(grid);
	for (std::size_t k = 0; k < 3; ++k)
	{
		std::vector<bool> seen(grid.cells(), false);
		for (int row = 0; row < 2; ++row)
		{
			for (int column = 0; column < 3; ++column)
				seen[std::size_t(row) * 3 + std::size_t(column)] = std::size_t(column + row) > k;
		}
		map.add(core::Uuid::generate(), seen);
	}

	const int scale = 4;
	const image::Image picture = ::rigplan::visibilityPicture(map, scale);
	REQUIRE(picture.valid());
	REQUIRE(picture.width() == 12);
	REQUIRE(picture.height() == 8);
	int wrong = 0;
	for (int y = 0; y < 8; ++y)
	{
		for (int x = 0; x < 12; ++x)
		{
			const std::uint8_t* p = picture.data() + 4 * (y * 12 + x);
			const int column = x / scale, row = 1 - y / scale; // north up
			const std::array<std::uint8_t, 3> colour = ::rigplan::visibilityColour(map.count(column, row));
			wrong += p[0] != colour[0] || p[1] != colour[1] || p[2] != colour[2] || p[3] != 255 ? 1 : 0;

			// The cell under that texel's centre is the one it was drawn for.
			const auto cell = ::rigplan::cellUnder(grid, (x + 0.5) / 12, (y + 0.5) / 8);
			REQUIRE(cell);
			wrong += cell->first != column || cell->second != row ? 1 : 0;
		}
	}
	CHECK(wrong == 0);
	CHECK(map.count(2, 1) == 3);
	CHECK_FALSE(::rigplan::cellUnder(grid, 1.0, 0.5));
	CHECK_FALSE(::rigplan::cellUnder(grid, 0.5, -0.01));

	// About a texel a pixel, and never a side past 2048.
	rp::VisibilityGrid room;
	room.columns = 400;
	room.rows = 300;
	CHECK(::rigplan::visibilityPictureScale(room, 600) == 2);
	CHECK(::rigplan::visibilityPictureScale(room, 100) == 1);
	CHECK(::rigplan::visibilityPictureScale(room, 5000) == 5);
}
