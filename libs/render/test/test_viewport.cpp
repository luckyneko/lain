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
};
static void submit(acm::Device& device, const GpuScene& scene, const std::vector<Recording>& recordings)
{
	std::vector<std::string> problems;
	const acm::Error error = device.submitSync(
		[&](acm::CommandBuffer& cmd)
		{
			for (const Recording& r : recordings)
			{
				for (std::string& problem : r.viewport->record(cmd, r.frame, scene, *r.view, r.lines))
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
	submit(*device, scene, {{&viewport, 0, &view, {}}});
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
	submit(*device, scene, {{&viewport, 0, &view, lines}});
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
		submit(*device, scene, {{&viewport, 0, &view, lines}});
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
	submit(*device, scene, {{&viewport, 0, &facing, {}}, {&viewport, 1, &aside, {}}});

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
	submit(*device, scene, {{&left, 0, &facing, {}}, {&right, 0, &aside, {}}});

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
	submit(*device, red, {{&viewport, 0, &view, {}}});
	CHECK(pixel(viewport.read(0), 32, 32)[0] == 255);
	submit(*device, green, {{&viewport, 0, &view, {}}});
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
	blind.near = 0;
	const View view = testing::view(64, 64, 64, 31.5, 31.5);

	std::vector<std::vector<std::string>> problems;
	const acm::Error error = device->submitSync(
		[&](acm::CommandBuffer& cmd)
		{
			problems.push_back(viewport.record(cmd, acm::Renderer::MaxFramesInFlight, scene, view, {}));
			problems.push_back(viewport.record(cmd, 0, stranger, view, {}));
			problems.push_back(viewport.record(cmd, 0, scene, blind, {}));
		});
	REQUIRE_FALSE(error);
	for (const std::vector<std::string>& refused : problems)
		CHECK(refused.size() == 1);
	CHECK_FALSE(viewport.read(0).valid()); // nothing was drawn
}
