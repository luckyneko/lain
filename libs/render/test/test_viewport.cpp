// Integration: a viewport recorded into a command buffer as a window's pre-pass would be, then read
// back as a pane would show it. SKIPs where the machine has no device.

#include "gpu.h"
#include "testscene.h"

#include <lain/image/transfercurve.h>
#include <lain/render/viewport.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::render;

#define REQUIRE_RENDERER(name)                    \
	acm::Device* device = testing::device();      \
	if (!device)                                  \
		SKIP("no Vulkan device on this machine"); \
	Renderer name(*device);                       \
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

TEST_CASE("a line drawn on a surface seen obliquely is drawn along its whole length", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Viewport viewport(renderer);

	// A floor 0.5 m below the camera, running away from it, as a viewport's grid lies on the floor.
	Scene scene;
	scene.meshes = {testing::square()};
	Material floor;
	floor.diffuse = {0.5f, 0.5f, 0.5f};
	scene.materials = {floor};
	math::Mat4d placement = math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.5, 4.0});
	placement = math::rotate(placement, 3.14159265358979323846 / 2, math::Vec3d{1.0, 0.0, 0.0});
	placement = math::scale(placement, math::Vec3d{3.0});
	scene.instances = {Instance{0, placement, {0}}};
	scene.ambient = {1.0f, 1.0f, 1.0f};
	const GpuScene gpu = uploaded(renderer, scene);
	const View view = testing::view(128, 128, 96, 63.5, 63.5);

	// Lines on the floor: one running away from the camera, and one across it.
	const std::vector<Segment> lines = {
		{{0.3, 0.5, 1.2}, {0.3, 0.5, 6.8}, {1.0f, 0.0f, 0.0f}},
		{{-2.5, 0.5, 2.5}, {2.5, 0.5, 2.5}, {1.0f, 0.0f, 0.0f}},
	};
	submit(*device, gpu, {{&viewport, 0, &view, lines}});
	const image::Image picture = viewport.read(0);
	REQUIRE(picture.valid());

	// Every pixel lain's projection puts each line through, away from its ends, shows it.
	const camera::CameraModel model = testing::pinholeOf(view);
	int samples = 0, drawn = 0;
	for (const Segment& line : lines)
	{
		for (int i = 5; i <= 95; ++i)
		{
			const math::Vec3d p = line.from + (line.to - line.from) * (i / 100.0);
			const camera::Projection<double> at = camera::project(model, p.x, p.y, p.z);
			REQUIRE(at.ok());
			const int x = int(std::lround(at.u));
			const int y = int(std::lround(at.v));
			if (std::abs(at.u - x) > 0.25 || std::abs(at.v - y) > 0.25 || x < 0 || y < 0 || x >= 128 || y >= 128)
				continue; // between pixels, where a 1 px line may fall either side
			++samples;
			drawn += pixel(picture, x, y)[0] > 200 && pixel(picture, x, y)[1] < 60 ? 1 : 0;
		}
	}
	CHECK(samples > 20);
	CHECK(drawn == samples);
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
