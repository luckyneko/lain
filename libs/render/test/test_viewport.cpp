// Integration: a viewport recorded into a command buffer as a window's pre-pass would be, then read
// back as a pane would show it. SKIPs where the machine has no device.

#include "gpu.h"
#include "testscene.h"

#include <lain/image/transfercurve.h>
#include <lain/render/viewport.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::render;

// The renderer under test, on each kind of device this machine offers (testing::device()).
#define REQUIRE_RENDERER(name)                \
	acm::Device* device = &testing::device(); \
	Renderer name(*device);                   \
	REQUIRE(name.problems().empty())

static GpuScene uploaded(Renderer& renderer, const Scene& scene)
{
	UploadResult result = renderer.upload(scene);
	REQUIRE(result.problems.empty());
	return std::move(*result.scene);
}

// One command buffer recording each (viewport, frame, view), as one window frame would, then submitted.
struct Recording
{
	Viewport* viewport;
	std::uint32_t frame;
	const View* view;
	std::vector<Segment> lines;
	std::optional<Grid> grid;
};
static void submit(acm::Device& device, const GpuScene& scene, const std::vector<Recording>& recordings)
{
	std::vector<std::string> problems;
	const acm::Error error = device.submitSync(
		[&](acm::CommandBuffer& cmd)
		{
			for (const Recording& r : recordings)
			{
				for (std::string& problem : r.viewport->record(cmd, r.frame, scene, *r.view, r.lines, r.grid))
					problems.push_back(std::move(problem));
			}
		});
	REQUIRE_FALSE(error);
	REQUIRE(problems.empty());
}

static std::array<int, 4> pixel(const image::Image& picture, int x, int y)
{
	const std::uint8_t* p = picture.data() + 4 * (std::size_t(y) * picture.width() + x);
	return {p[0], p[1], p[2], p[3]};
}

// Linear light as an 8-bit sRGB code, through lain's own curve.
static int encoded(float linear)
{
	return int(std::lround(image::transferCurve(image::ColorSpace::sRGB).fromLinear(linear) * 255));
}

static bool near(int actual, int expected)
{
	return std::abs(actual - expected) <= 1;
}

// A grey wall half a metre each side of the axis, 2 m away, in front of a blue background.
static Scene wall(image::ColorRGBf colour = {0.5f, 0.5f, 0.5f})
{
	Scene scene;
	scene.meshes = {testing::square()};
	Material material;
	material.diffuse = colour;
	scene.materials = {material};
	scene.instances = {Instance{0, math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 2.0}), math::Vec3d{0.5}), {0}}};
	scene.ambient = {1.0f, 1.0f, 1.0f};
	scene.background = {0.05f, 0.1f, 0.8f};
	return scene;
}

TEST_CASE("a viewport shows its scene through lain's sRGB curve", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);
	REQUIRE(viewport.problems().empty());

	const GpuScene scene = uploaded(renderer, wall({0.2f, 0.5f, 0.05f}));
	const View view = testing::view(64, 64, 64, 31.5, 31.5);
	submit(*device, scene, {{&viewport, 0, &view, {}, std::nullopt}});
	CHECK(viewport.texture(0).valid());
	const image::Image picture = viewport.read(0);
	REQUIRE(picture.valid());
	CHECK(picture.colorSpace() == image::ColorSpace::sRGB);

	const std::array<int, 4> centre = pixel(picture, 32, 32);
	CHECK(near(centre[0], encoded(0.2f)));
	CHECK(near(centre[1], encoded(0.5f)));
	CHECK(near(centre[2], encoded(0.05f)));
	CHECK(centre[3] == 255);
	const std::array<int, 4> corner = pixel(picture, 2, 2);
	CHECK(near(corner[0], encoded(0.05f)));
	CHECK(near(corner[1], encoded(0.1f)));
	CHECK(near(corner[2], encoded(0.8f)));
	CHECK(corner[3] == 255);
}

TEST_CASE("a line in front of a surface is drawn, one behind it hidden, and one on it drawn", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);
	REQUIRE(viewport.problems().empty());

	const GpuScene scene = uploaded(renderer, wall());
	const View view = testing::view(64, 64, 64, 31.5, 31.5);
	// A horizontal line across the whole view at `depth`, along pixel row `row`.
	auto across = [&](double depth, int row, image::ColorRGBf colour)
	{
		const double y = (row - 31.5) * depth / 64;
		const double x = 32 * depth / 64;
		return Segment{{-x, y, depth}, {x, y, depth}, colour};
	};
	const std::vector<Segment> lines = {
		across(1.5, 24, {1.0f, 0.0f, 0.0f}),  // in front of the wall
		across(2.05, 32, {0.0f, 1.0f, 0.0f}), // 5 cm behind it
		across(2.0, 40, {0.0f, 0.0f, 1.0f}),  // on it
	};
	submit(*device, scene, {{&viewport, 0, &view, lines, std::nullopt}});
	const image::Image picture = viewport.read(0);
	REQUIRE(picture.valid());

	const int grey = encoded(0.5f);
	// The wall covers pixels 16 to 47 each way.
	const std::array<int, 4> front = pixel(picture, 31, 24);
	CHECK(front[0] == 255);
	CHECK(front[1] == 0);
	const std::array<int, 4> hidden = pixel(picture, 31, 32);
	CHECK(near(hidden[0], grey));
	CHECK(near(hidden[1], grey));
	const std::array<int, 4> beside = pixel(picture, 6, 32); // past the wall's edge, nothing hides it
	CHECK(beside[1] == 255);
	CHECK(beside[0] == 0);
	const std::array<int, 4> on = pixel(picture, 31, 40);
	CHECK(on[2] == 255);
	CHECK(on[0] == 0);
	const std::array<int, 4> below = pixel(picture, 31, 42); // a line is one pixel wide
	CHECK(near(below[2], grey));
}

TEST_CASE("a line drawn on a surface seen obliquely is never lost to it", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	// A floor `drop` metres below lines 0.5 m below the camera, running away from it, as a viewport's
	// grid lies on the floor. Unlit, so it is one grey everywhere.
	auto floorAt = [&](double drop)
	{
		Scene scene;
		scene.meshes = {testing::square()};
		Material floor;
		floor.diffuse = {0.5f, 0.5f, 0.5f};
		scene.materials = {floor};
		// 8 m across and from 1 m to 12 m ahead, so its edges are pixels away from the lines in the
		// picture and the drop moves none of them through a pixel compared.
		math::Mat4d placement = math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.5 + drop, 6.5});
		placement = math::rotate(placement, 3.14159265358979323846 / 2, math::Vec3d{1.0, 0.0, 0.0});
		placement = math::scale(placement, math::Vec3d{4.0, 5.5, 1.0});
		scene.instances = {Instance{0, placement, {0}}};
		scene.ambient = {1.0f, 1.0f, 1.0f};
		return scene;
	};
	const View view = testing::view(128, 128, 96, 63.5, 63.5);
	const std::vector<Segment> lines = {
		{{0.3, 0.5, 1.2}, {0.3, 0.5, 6.8}, {1.0f, 0.0f, 0.0f}},	 // away from the camera
		{{-2.5, 0.5, 2.5}, {2.5, 0.5, 2.5}, {1.0f, 0.0f, 0.0f}}, // across
	};
	auto drawn = [&](double drop)
	{
		const GpuScene scene = uploaded(renderer, floorAt(drop));
		submit(*device, scene, {{&viewport, 0, &view, lines, std::nullopt}});
		return viewport.read(0);
	};
	// The floor under the lines, and 5 cm below them, where it cannot hide any part of them. A device
	// may spread a 1 px line's coverage over neighbouring pixels (the CPU device does), so the line is
	// compared with itself rather than judged against a full pixel of red.
	const image::Image on = drawn(0.0);
	const image::Image clear = drawn(0.05);
	REQUIRE(on.valid());
	REQUIRE(clear.valid());

	// A 1 px line's depth is the same across its width while an oblique floor's is not, and under
	// multisampling a line's samples lie up to 0.375 px off its centre, where the floor is nearer. So
	// the floor wins some of them, on every device, with the pull or without (shaders/lines.vert):
	// measured, Metal loses up to half a pixel's coverage and the CPU device three quarters. The grid
	// is therefore not drawn as lines on the floor (slice 8). What is guaranteed is that such a line is
	// never lost: at every point along it, its 3 x 3 pixels keep a quarter of the coverage they have
	// with the floor out of its way. Coverage is read from the green channel, which the red line lacks:
	// the picture is resolved in its encoded values, so a pixel the line covers a fraction c of is
	// (1 - c) of the floor's green.
	const camera::CameraModel model = testing::pinholeOf(view);
	const double floorGreen = encoded(0.5f);
	auto coverage = [&](const image::Image& picture, int x, int y)
	{ return 1 - pixel(picture, x, y)[1] / floorGreen; };
	int points = 0;
	double worstKept = 1;
	for (const Segment& line : lines)
	{
		for (int i = 5; i <= 95; ++i)
		{
			const math::Vec3d p = line.from + (line.to - line.from) * (i / 100.0);
			const camera::Projection<double> at = camera::project(model, p.x, p.y, p.z);
			REQUIRE(at.ok());
			double kept = 0, there = 0;
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int x = int(std::lround(at.u)) + dx;
					const int y = int(std::lround(at.v)) + dy;
					if (x < 0 || y < 0 || x >= 128 || y >= 128)
						continue;
					kept += coverage(on, x, y);
					there += coverage(clear, x, y);
				}
			}
			if (there < 0.5)
				continue; // off the picture's edge
			++points;
			worstKept = std::min(worstKept, kept / there);
		}
	}
	CHECK(points > 100);
	CHECK(worstKept >= 0.25);
}

TEST_CASE("a viewport's frames in flight each keep their own picture", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);
	REQUIRE(viewport.problems().empty());

	const GpuScene scene = uploaded(renderer, wall());
	const View facing = testing::view(64, 64, 64, 31.5, 31.5);
	View aside = testing::view(32, 32, 32, 15.5, 15.5);
	aside.worldFromCamera = math::RigidTransformd{math::Quatd{1.0, 0.0, 0.0, 0.0}, math::Vec3d{10.0, 0.0, 0.0}};
	// Both frames in one command buffer, as a window's two frames in flight would overlap.
	submit(*device, scene, {{&viewport, 0, &facing, {}, std::nullopt}, {&viewport, 1, &aside, {}, std::nullopt}});

	const image::Image first = viewport.read(0);
	const image::Image second = viewport.read(1);
	REQUIRE(first.width() == 64);
	REQUIRE(second.width() == 32);
	CHECK(near(pixel(first, 32, 32)[0], encoded(0.5f)));  // the wall
	CHECK(near(pixel(second, 16, 16)[2], encoded(0.8f))); // the background: the wall is 10 m to the side
	CHECK(near(pixel(second, 16, 16)[0], encoded(0.05f)));
}

TEST_CASE("two viewports of one scene in one frame each keep their own view", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport left(renderer);
	Viewport right(renderer);

	const GpuScene scene = uploaded(renderer, wall());
	const View facing = testing::view(64, 64, 64, 31.5, 31.5);
	View aside = facing;
	aside.worldFromCamera = math::RigidTransformd{math::Quatd{1.0, 0.0, 0.0, 0.0}, math::Vec3d{10.0, 0.0, 0.0}};
	submit(*device, scene, {{&left, 0, &facing, {}, std::nullopt}, {&right, 0, &aside, {}, std::nullopt}});

	CHECK(near(pixel(left.read(0), 32, 32)[0], encoded(0.5f)));
	CHECK(near(pixel(right.read(0), 32, 32)[0], encoded(0.05f)));
}

TEST_CASE("a viewport draws the scene it is given, not the one it last drew", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	const View view = testing::view(64, 64, 64, 31.5, 31.5);
	const GpuScene red = uploaded(renderer, wall({1.0f, 0.0f, 0.0f}));
	const GpuScene green = uploaded(renderer, wall({0.0f, 1.0f, 0.0f})); // both kept alive
	submit(*device, red, {{&viewport, 0, &view, {}, std::nullopt}});
	CHECK(pixel(viewport.read(0), 32, 32)[0] == 255);
	submit(*device, green, {{&viewport, 0, &view, {}, std::nullopt}});
	const std::array<int, 4> now = pixel(viewport.read(0), 32, 32);
	CHECK(now[0] == 0);
	CHECK(now[1] == 255);
}

TEST_CASE("a viewport refuses what it cannot draw, and says why", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	CHECK_FALSE(viewport.read(0).valid()); // nothing recorded yet
	CHECK_FALSE(viewport.texture(acm::Renderer::MaxFramesInFlight).valid());

	const GpuScene scene = uploaded(renderer, wall());
	Renderer other(*device);
	const GpuScene stranger = uploaded(other, wall());
	View blind = testing::view(64, 64, 64, 31.5, 31.5);
	blind.nearPlane = 0;
	const View view = testing::view(64, 64, 64, 31.5, 31.5);

	std::vector<std::vector<std::string>> problems;
	const acm::Error error = device->submitSync(
		[&](acm::CommandBuffer& cmd)
		{
			problems.push_back(viewport.record(cmd, acm::Renderer::MaxFramesInFlight, scene, view, {}, std::nullopt));
			problems.push_back(viewport.record(cmd, 0, stranger, view, {}, std::nullopt));
			problems.push_back(viewport.record(cmd, 0, scene, blind, {}, std::nullopt));
			Grid flat; // no spacing between its lines
			flat.spacing = 0;
			problems.push_back(viewport.record(cmd, 0, scene, view, {}, flat));
		});
	REQUIRE_FALSE(error);
	for (const std::vector<std::string>& refused : problems)
		CHECK(refused.size() == 1);
	CHECK_FALSE(viewport.read(0).valid()); // nothing was drawn
}

TEST_CASE("a viewport draws a scene as an update left it", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	const View view = testing::view(64, 64, 64, 31.5, 31.5);
	const Scene red = wall({1.0f, 0.0f, 0.0f});
	GpuScene scene = uploaded(renderer, red);
	submit(*device, scene, {{&viewport, 0, &view, {}, std::nullopt}});
	CHECK(pixel(viewport.read(0), 32, 32)[0] == 255);

	// The same mesh, another material: the viewport's sets for the old one must not survive the update.
	Scene green = red;
	green.materials[0].diffuse = {0.0f, 1.0f, 0.0f};
	const UpdateResult updated = renderer.update(scene, green);
	REQUIRE(updated.problems.empty());
	CHECK(updated.meshesUploaded == 0);
	submit(*device, scene, {{&viewport, 0, &view, {}, std::nullopt}});
	const std::array<int, 4> now = pixel(viewport.read(0), 32, 32);
	CHECK(now[0] == 0);
	CHECK(now[1] == 255);
}

// --- The grid (render::Grid) ------------------------------------------------------------------------

// A floor 8 m across and from 1 m to 12 m ahead, `height` metres below a camera at rest (the camera
// frame's +Y is down), lit by ambient light alone so it is `colour` everywhere; or no floor when
// `height` is empty. Over a background of `background`.
static Scene floorScene(std::optional<double> height, image::ColorRGBf colour, image::ColorRGBf background)
{
	Scene scene;
	scene.ambient = {1.0f, 1.0f, 1.0f};
	scene.background = background;
	if (!height)
		return scene;
	scene.meshes = {testing::square()};
	Material floor;
	floor.diffuse = colour;
	scene.materials = {floor};
	math::Mat4d placement = math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, *height, 6.5});
	placement = math::rotate(placement, 3.14159265358979323846 / 2, math::Vec3d{1.0, 0.0, 0.0});
	placement = math::scale(placement, math::Vec3d{4.0, 5.5, 1.0});
	scene.instances = {Instance{0, placement, {0}}};
	return scene;
}

// A grid on the horizontal plane `height` metres below a camera at rest: its lines run across (X) and
// away (Z), `spacing` apart, in `colour`, with no major lines.
static Grid gridBelow(double height, double spacing, image::ColorRGBf colour)
{
	Grid grid;
	grid.origin = {0.0, height, 0.0};
	grid.u = {1.0, 0.0, 0.0};
	grid.v = {0.0, 0.0, 1.0};
	grid.spacing = spacing;
	grid.majorEvery = 0;
	grid.extent = {20.0, 20.0};
	grid.minor = colour;
	return grid;
}

TEST_CASE("a grid line on a surface seen obliquely is drawn as it is with nothing under it", "[render][gpu]")
{
	// What a 1 px overlay line cannot do (the oblique-floor case above: it keeps as little as a quarter
	// of its coverage). A floor lying on the grid's plane draws the grid's lines itself, and the grid's
	// backdrop draws them where nothing is; both mix in the display encoding, so over a floor the colour
	// of the background, a line drawn on the floor and a line drawn with no floor are one line.
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);
	REQUIRE(viewport.problems().empty());

	const image::ColorRGBf grey{0.5f, 0.5f, 0.5f};
	const View view = testing::view(128, 128, 96, 63.5, 63.5);
	const Grid grid = gridBelow(0.5, 0.5, {1.0f, 0.0f, 0.0f});
	auto drawn = [&](std::optional<double> floor)
	{
		const GpuScene scene = uploaded(renderer, floorScene(floor, grey, grey));
		submit(*device, scene, {{&viewport, 0, &view, {}, grid}});
		return viewport.read(0);
	};
	const image::Image on = drawn(0.5);				// the floor on the grid's plane
	const image::Image clear = drawn(std::nullopt); // no floor: the backdrop alone
	REQUIRE(on.valid());
	REQUIRE(clear.valid());

	// Coverage from the green channel, which the red line lacks: mixed in the encoding, a pixel the line
	// covers a fraction c of is (1 - c) of the grey's green.
	const double greyGreen = encoded(0.5f);
	auto coverage = [&](const image::Image& picture, int x, int y)
	{ return 1 - pixel(picture, x, y)[1] / greyGreen; };
	int lined = 0;
	double worst = 0;
	for (int y = 0; y < 128; ++y)
	{
		for (int x = 0; x < 128; ++x)
		{
			const double without = coverage(clear, x, y);
			lined += without > 0.5 ? 1 : 0;
			worst = std::max(worst, std::abs(coverage(on, x, y) - without));
		}
	}
	INFO("pixels a line covers more than half of: " << lined << "; worst difference in coverage: " << worst);
	CHECK(lined > 500); // the grid is there to compare: measured 628 on Metal, 684 on the CPU device
	// Measured 0.0053 on Metal and 0.032 on the CPU device: the same mix in the same encoding, drawn by
	// different triangles (the floor's and the backdrop's), whose screen-space derivatives, which the
	// anti-aliasing reads, need not agree to the last bit. Not chased further. An overlay line on the
	// same floor keeps as little as a quarter of its coverage.
	CHECK(worst <= 0.05);
}

// The red code of the pixel lain's projection puts `point` in, and the most red among the 3 x 3 pixels
// around it.
static int redAt(const image::Image& picture, const camera::CameraModel& model, const math::Vec3d& point)
{
	const camera::Projection<double> p = camera::project(model, point.x, point.y, point.z);
	REQUIRE(p.ok());
	return pixel(picture, int(std::lround(p.u)), int(std::lround(p.v)))[0];
}
static int redNear(const image::Image& picture, const camera::CameraModel& model, const math::Vec3d& point)
{
	const camera::Projection<double> p = camera::project(model, point.x, point.y, point.z);
	REQUIRE(p.ok());
	int most = 0;
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int x = int(std::lround(p.u)) + dx;
			const int y = int(std::lround(p.v)) + dy;
			if (x >= 0 && y >= 0 && x < picture.width() && y < picture.height())
				most = std::max(most, pixel(picture, x, y)[0]);
		}
	}
	return most;
}

TEST_CASE("a grid's lines fall where lain's projection puts the plane's lines", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	const View view = testing::view(128, 128, 96, 63.5, 63.5);
	const GpuScene scene = uploaded(renderer, floorScene(std::nullopt, {}, {0.0f, 0.0f, 0.0f}));
	submit(*device, scene, {{&viewport, 0, &view, {}, gridBelow(0.5, 0.5, {1.0f, 0.0f, 0.0f})}});
	const image::Image picture = viewport.read(0);
	REQUIRE(picture.valid());

	// A line is 1 px wide and fades over a pixel each side. Pixel centres lie at most half a pixel
	// apart across any line, so among the 3 x 3 pixels around a point of it, one is at least half
	// covered: red at least half of 255 over the black. Halfway between lines there is none.
	const camera::CameraModel model = testing::pinholeOf(view);
	int checked = 0;
	for (int tenths = 12; tenths <= 40; ++tenths) // along the line x = 0.5, running away
	{
		const double z = tenths / 10.0;
		INFO("x = 0.5, z = " << z);
		CHECK(redNear(picture, model, {0.5, 0.5, z}) >= 128);
		++checked;
	}
	for (int tenths = -10; tenths <= 10; ++tenths) // along the line z = 1.5, running across
	{
		const double x = tenths / 10.0;
		INFO("x = " << x << ", z = 1.5");
		CHECK(redNear(picture, model, {x, 0.5, 1.5}) >= 128);
		++checked;
	}
	for (const double x : {-0.75, -0.25, 0.25, 0.75}) // halfway between lines both ways
	{
		for (const double z : {1.25, 1.75})
		{
			INFO("between lines at x = " << x << ", z = " << z);
			CHECK(redAt(picture, model, {x, 0.5, z}) <= 2);
			++checked;
		}
	}
	CHECK(checked == 29 + 21 + 8);
}

TEST_CASE("a wall standing on the grid hides the grid behind it, and not the grid in front of it", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	// A green wall 2 m ahead, its foot on the grid's plane 0.8 m below the camera, so the grid behind it
	// falls on its pixels and the grid in front of it below them. The points are where the grid's lines
	// are well apart on screen, which the same view without the wall shows: further down the plane the
	// lines fade out (grid.glsl), and there would be nothing for the wall to hide.
	auto drawn = [&](bool withWall)
	{
		Scene scene;
		scene.ambient = {1.0f, 1.0f, 1.0f};
		scene.background = {0.0f, 0.0f, 0.3f};
		if (withWall)
		{
			scene.meshes = {testing::square()};
			Material green;
			green.diffuse = {0.0f, 0.5f, 0.0f};
			scene.materials = {green};
			math::Mat4d placement = math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.15, 2.0});
			placement = math::scale(placement, math::Vec3d{1.0, 0.65, 1.0}); // x from -1 to 1, y from -0.5 to 0.8
			scene.instances = {Instance{0, placement, {0}}};
		}
		const GpuScene gpu = uploaded(renderer, scene);
		const View view = testing::view(96, 96, 64, 47.5, 47.5);
		submit(*device, gpu, {{&viewport, 0, &view, {}, gridBelow(0.8, 0.5, {1.0f, 0.0f, 0.0f})}});
		return viewport.read(0);
	};
	const image::Image open = drawn(false);
	const image::Image walled = drawn(true);
	REQUIRE(open.valid());
	REQUIRE(walled.valid());

	const camera::CameraModel model = testing::pinholeOf(testing::view(96, 96, 64, 47.5, 47.5));
	for (const double z : {2.25, 2.5, 2.75}) // behind the wall, on the line x = 0.5
	{
		INFO("behind, z = " << z);
		CHECK(redNear(open, model, {0.5, 0.8, z}) >= 128); // there to be hidden
		const camera::Projection<double> p = camera::project(model, 0.5, 0.8, z);
		REQUIRE(p.ok());
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx) // the wall's green, and no red, all round the point
			{
				const std::array<int, 4> behind = pixel(walled, int(std::lround(p.u)) + dx, int(std::lround(p.v)) + dy);
				CHECK(behind[0] <= 1);
				CHECK(near(behind[1], encoded(0.5f)));
			}
		}
	}
	for (const double z : {1.5, 1.75}) // in front of it: below its foot, red
	{
		INFO("in front, z = " << z);
		CHECK(redNear(walled, model, {0.5, 0.8, z}) >= 128);
	}
}

TEST_CASE("a grid draws its lines over a surface behind its plane, and nothing over one in front", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	const View view = testing::view(128, 128, 96, 63.5, 63.5);
	const Grid grid = gridBelow(0.5, 0.5, {1.0f, 0.0f, 0.0f});
	const image::ColorRGBf green{0.0f, 0.5f, 0.0f};
	const camera::CameraModel model = testing::pinholeOf(view);
	auto drawnWithFloorAt = [&](double height)
	{
		const GpuScene scene = uploaded(renderer, floorScene(height, green, {0.0f, 0.0f, 0.3f}));
		submit(*device, scene, {{&viewport, 0, &view, {}, grid}});
		return viewport.read(0);
	};
	// 5 cm below the plane, seen through it: the lines are drawn over the floor, and the floor shows
	// between them, since the backdrop is transparent there.
	const image::Image below = drawnWithFloorAt(0.55);
	REQUIRE(below.valid());
	for (const double z : {2.0, 3.0})
	{
		INFO("on the line x = 0.5 at z = " << z);
		CHECK(redNear(below, model, {0.5, 0.5, z}) >= 128);
	}
	for (const double x : {-0.25, 0.25})
	{
		INFO("between lines at x = " << x);
		const camera::Projection<double> p = camera::project(model, x, 0.5, 1.75);
		REQUIRE(p.ok());
		const std::array<int, 4> floor = pixel(below, int(std::lround(p.u)), int(std::lround(p.v)));
		CHECK(floor[0] <= 1);
		CHECK(floor[1] >= encoded(0.5f) - 8); // the floor, a few codes off where a line's fade reaches
	}

	// 5 cm above the plane, in front of it: nowhere the floor covers does any of the grid show. (Its
	// near edge is 1 m ahead, and the grid shows below that, where nothing is in front of it.)
	const image::Image above = drawnWithFloorAt(0.45);
	REQUIRE(above.valid());
	for (double z = 1.5; z <= 6.0; z += 0.25)
	{
		INFO("on the line x = 0.5 at z = " << z);
		CHECK(redNear(above, model, {0.5, 0.5, z}) <= 1);
	}
}

TEST_CASE("neither a render nor a capture shows the grid a viewport shows", "[render][gpu]")
{
	// A grid is a viewport's alone: render() and capture() take none, so the frame constants they write
	// say there is none, whatever a viewport of the same scene shows. Here the grid is the default
	// Grid's plane, Z = 0, with a camera 2 m above it looking down, so a grid drawn by default would land
	// on the floor.
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	Scene scene;
	scene.meshes = {testing::square()};
	Material floor;
	floor.diffuse = {0.2f, 0.6f, 0.3f};
	scene.materials = {floor};
	scene.instances = {Instance{0, math::scale(math::Mat4d{1.0}, math::Vec3d{3.0, 3.0, 1.0}), {0}}};
	scene.ambient = {1.0f, 1.0f, 1.0f};
	View view = testing::view(64, 64, 48, 31.5, 31.5);
	view.worldFromCamera = math::RigidTransformd{math::angleAxis(3.14159265358979323846, math::Vec3d{1.0, 0.0, 0.0}), math::Vec3d{0.0, 0.0, 2.0}};
	Grid grid; // the default: Z = 0, lines every 0.1 m
	grid.minor = {1.0f, 0.0f, 0.0f};
	grid.major = {1.0f, 0.0f, 0.0f};

	GpuScene gpu = uploaded(renderer, scene);
	submit(*device, gpu, {{&viewport, 0, &view, {}, grid}});
	const image::Image shown = viewport.read(0);
	REQUIRE(shown.valid());
	int red = 0;
	for (int y = 0; y < 64; ++y)
	{
		for (int x = 0; x < 64; ++x)
			red = std::max(red, pixel(shown, x, y)[0]);
	}
	// The viewport shows it: a major line every metre, over a floor whose red alone is encoded(0.2),
	// 124. (A line straddling two pixels covers each by half, measured 189.)
	CHECK(red > 160);

	// The floor fills both pictures, so every pixel is its colour, and no line is anywhere.
	auto worstOffFloor = [&](const image::Image& picture)
	{
		double worst = 0;
		for (int y = 0; y < picture.height(); ++y)
		{
			for (int x = 0; x < picture.width(); ++x)
			{
				const image::ColorRGBAf p = testing::pixel(picture, x, y);
				worst = std::max({worst, std::abs(p.r - 0.2), std::abs(p.g - 0.6), std::abs(p.b - 0.3)});
			}
		}
		return worst;
	};
	const RenderResult rendered = renderer.render(gpu, view);
	REQUIRE(rendered.problems.empty());
	CHECK(worstOffFloor(rendered.image) < 1e-3);

	const RayTableResult table = RayTable::build(testing::pinholeOf(view), {});
	REQUIRE(table.table.has_value());
	GpuRayTableResult rays = renderer.upload(*table.table);
	REQUIRE(rays.problems.empty());
	CaptureView pose;
	pose.worldFromCamera = view.worldFromCamera;
	const RenderResult captured = renderer.capture(gpu, *rays.table, pose);
	REQUIRE(captured.problems.empty());
	CHECK(worstOffFloor(captured.image) < 1e-3);
}
