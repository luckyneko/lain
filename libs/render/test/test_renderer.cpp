// Integration: scenes drawn on a real device and read back, each judged against what lain's own
// geometry and the shading model say the picture must be. SKIPs where the machine has no device.

#include "gpu.h"
#include "testscene.h"

#include <lain/render/renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace lain;
using namespace lain::render;

// The renderer under test, on each kind of device this machine offers (testing::device()).
#define REQUIRE_RENDERER(name)                \
	acm::Device* device = &testing::device(); \
	Renderer name(*device);                   \
	REQUIRE(name.problems().empty())

static image::Image draw(Renderer& renderer, const Scene& scene, const View& view)
{
	UploadResult uploaded = renderer.upload(scene);
	REQUIRE(uploaded.problems.empty());
	REQUIRE(uploaded.scene);
	RenderResult drawn = renderer.render(*uploaded.scene, view);
	REQUIRE(drawn.problems.empty());
	REQUIRE(drawn.image.valid());
	return std::move(drawn.image);
}

static Material plain(image::ColorRGBf diffuse)
{
	Material material;
	material.diffuse = diffuse;
	return material;
}

// The point a pixel's centre sees on the plane Z = depth, for a camera at rest.
static math::Vec3d onPlane(const View& view, int x, int y, double depth)
{
	const camera::Intrinsics& k = view.intrinsics;
	return math::Vec3d{(x - k.cx) / k.fx, (y - k.cy) / k.fy, 1.0} * depth;
}

static double relativeError(double actual, double expected)
{
	return std::abs(actual - expected) / std::max(std::abs(expected), 1e-6);
}

TEST_CASE("a surface covers exactly the pixel centres its projected outline encloses", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	// A tilted, stretched square off the axis, so its outline is a general quadrilateral, seen
	// through a pinhole whose principal point is not the image's centre.
	Scene scene;
	scene.meshes = {testing::square()};
	scene.materials = {plain({1.0f, 1.0f, 1.0f})};
	math::Mat4d placement = math::translate(math::Mat4d{1.0}, math::Vec3d{0.1, -0.05, 3.0});
	placement = math::rotate(placement, 0.4, math::Vec3d{0.0, 1.0, 0.0});
	placement = math::rotate(placement, -0.3, math::Vec3d{1.0, 0.0, 0.0});
	placement = math::scale(placement, math::Vec3d{0.8, 0.6, 1.0});
	scene.instances = {Instance{0, placement, {0}}};
	scene.ambient = {0.5f, 0.5f, 0.5f};
	scene.background = {0.1f, 0.2f, 0.3f};
	const View view = testing::view(96, 72, 80, 47.3, 35.6);

	const image::Image picture = draw(renderer, scene, view);
	REQUIRE(picture.width() == 96);
	REQUIRE(picture.height() == 72);

	// The outline lain's projection gives the corners. A pixel's centre is covered when it lies inside;
	// those within a thousandth of a pixel of an edge are left out, since a rasteriser's tie rule and
	// float arithmetic decide them.
	const camera::CameraModel model = testing::pinholeOf(view);
	std::array<math::Vec2d, 4> outline;
	const std::array<math::Vec3d, 4> corners = testing::squareCorners();
	for (std::size_t c = 0; c < 4; ++c)
	{
		const math::Vec3d world{placement * math::Vec4d{corners[c], 1.0}};
		const camera::Projection<double> p = camera::project(model, world.x, world.y, world.z);
		REQUIRE(p.ok());
		outline[c] = {p.u, p.v};
	}
	const double area = (outline[1].x - outline[0].x) * (outline[2].y - outline[0].y) -
						(outline[2].x - outline[0].x) * (outline[1].y - outline[0].y);

	int inside = 0, outside = 0, wrong = 0;
	for (int y = 0; y < 72; ++y)
	{
		for (int x = 0; x < 96; ++x)
		{
			double nearest = 1e9;
			bool enclosed = true;
			for (std::size_t c = 0; c < 4; ++c)
			{
				const math::Vec2d a = outline[c];
				const math::Vec2d b = outline[(c + 1) % 4];
				const math::Vec2d edge = b - a;
				const double side = (edge.x * (y - a.y) - edge.y * (x - a.x)) / math::length(edge) * (area > 0 ? 1 : -1);
				enclosed = enclosed && side > 0;
				nearest = std::min(nearest, std::abs(side));
			}
			if (nearest < 1e-3)
				continue;
			const image::ColorRGBAf p = testing::pixel(picture, x, y);
			const bool covered = p.a == 1.0f;
			(enclosed ? inside : outside) += 1;
			if (covered != enclosed)
				++wrong;
			else if (covered && std::abs(p.r - 0.5f) > 1e-6f)
				++wrong;
			else if (!covered && (p.a != 0.0f || std::abs(p.b - 0.3f) > 1e-6f))
				++wrong;
		}
	}
	CHECK(inside > 500);
	CHECK(outside > 500);
	CHECK(wrong == 0);
}

TEST_CASE("a nearer surface hides a farther one whichever is drawn first", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	Scene scene;
	scene.meshes = {testing::square()};
	scene.materials = {plain({1.0f, 0.0f, 0.0f}), plain({0.0f, 1.0f, 0.0f})};
	scene.ambient = {1.0f, 1.0f, 1.0f};
	const Instance nearer{0, math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 2.0}), math::Vec3d{0.5}), {0}};
	const Instance farther{0, math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 4.0}), math::Vec3d{3.0}), {1}};
	const View view = testing::view(64, 64, 64, 31.5, 31.5);

	for (const std::vector<Instance>& order : {std::vector<Instance>{nearer, farther}, std::vector<Instance>{farther, nearer}})
	{
		scene.instances = order;
		const image::Image picture = draw(renderer, scene, view);
		const image::ColorRGBAf centre = testing::pixel(picture, 32, 32);
		CHECK(centre.r == 1.0f);
		CHECK(centre.g == 0.0f);
		const image::ColorRGBAf rim = testing::pixel(picture, 4, 32); // beyond the near square's edge
		CHECK(rim.r == 0.0f);
		CHECK(rim.g == 1.0f);
	}
}

TEST_CASE("a surface reaching into the camera's own plane hides nothing in front of it", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	// A floor whose near edge lies at the camera's plane, or within a hair of it, as a wall's corners do
	// for a camera mounted flush on it, and a red square standing on the floor 3 m ahead. The CPU device
	// drew the floor over the square wherever the edge came within about 1e-5 of the floor's width of
	// the plane, until offPlane (shaders/frame.glsl) moved such vertices off it.
	const View view = testing::view(128, 128, 96, 63.5, 63.5);
	for (const double width : {10.0, 40.0})
	{
		for (const double edge : {0.0, 1e-6, -1e-6, 1e-4, -1e-4, 3e-4})
		{
			INFO("a floor " << 2 * width << " m wide, its near edge " << edge << " m from the camera's plane");
			Scene scene;
			scene.meshes = {testing::square()};
			scene.materials = {plain({0.5f, 0.5f, 0.5f}), plain({1.0f, 0.0f, 0.0f})};
			const double half = (20.0 - edge) / 2;
			math::Mat4d floor = math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.55, edge + half});
			floor = math::rotate(floor, 3.14159265358979323846 / 2, math::Vec3d{1.0, 0.0, 0.0});
			floor = math::scale(floor, math::Vec3d{width, half, 1.0});
			const math::Mat4d square = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.3, 3.0}), math::Vec3d{0.3});
			scene.instances = {Instance{0, floor, {0}}, Instance{0, square, {1}}};
			scene.ambient = {1.0f, 1.0f, 1.0f};
			const image::Image picture = draw(renderer, scene, view);
			const image::ColorRGBAf ahead = testing::pixel(picture, 64, 72); // the square's middle
			CHECK(ahead.r == 1.0f);
			CHECK(ahead.g == 0.0f);
			CHECK(testing::pixel(picture, 64, 110).g == 0.5f); // and the floor below it
		}
	}
}

TEST_CASE("a surface is lit as the inverse square and the angle of incidence say", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	const double depth = 2.0;
	const image::ColorRGBf albedo{0.6f, 0.5f, 0.4f};
	Light light;
	light.position = {0.5, -0.4, 1.0};
	light.colour = {1.0f, 0.8f, 0.6f};
	light.intensity = 3;
	const View view = testing::view(64, 48, 40, 31.5, 23.5);

	// The light a pixel's centre should receive from the plane Z = depth facing the camera, from a
	// light at `at`.
	auto expected = [&](int x, int y, const math::Vec3d& at, int channel)
	{
		const math::Vec3d p = onPlane(view, x, y, depth);
		const math::Vec3d toLight = at - p;
		const double distanceSquared = math::dot(toLight, toLight);
		const double lambert = std::max(0.0, -math::normalize(toLight).z); // the normal is -Z
		return albedo[channel] * light.colour[channel] * light.intensity * lambert / distanceSquared;
	};
	// Every pixel against `expected`: the plane fills the view.
	auto compare = [&](const image::Image& picture, const math::Vec3d& at)
	{
		double worst = 0;
		for (int y = 0; y < 48; ++y)
		{
			for (int x = 0; x < 64; ++x)
			{
				const image::ColorRGBAf p = testing::pixel(picture, x, y);
				REQUIRE(p.a == 1.0f);
				for (int c = 0; c < 3; ++c)
					worst = std::max(worst, relativeError(p[c], expected(x, y, at, c)));
			}
		}
		return worst;
	};

	Scene scene;
	scene.materials = {plain(albedo)};
	scene.lights = {light};
	const math::Mat4d placement = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, depth}), math::Vec3d{4.0});

	SECTION("with the mesh's own normals")
	{
		scene.meshes = {testing::square()};
		scene.instances = {Instance{0, placement, {0}}};
		CHECK(compare(draw(renderer, scene, view), light.position) < 1e-4);
	}
	SECTION("by the face's own normal, when the mesh gives none")
	{
		scene.meshes = {testing::square(false)};
		scene.instances = {Instance{0, placement, {0}}};
		CHECK(compare(draw(renderer, scene, view), light.position) < 1e-4);
	}
	SECTION("from the side the camera sees, when its normals face away")
	{
		scene.meshes = {testing::square(true, 1, {0.0f, 0.0f, 1.0f})};
		scene.instances = {Instance{0, placement, {0}}};
		CHECK(compare(draw(renderer, scene, view), light.position) < 1e-4);
	}
	SECTION("not at all, by a light behind it")
	{
		scene.meshes = {testing::square()};
		scene.instances = {Instance{0, placement, {0}}};
		scene.lights[0].position = {0.0, 0.0, 3.0};
		const image::Image picture = draw(renderer, scene, view);
		for (int y = 0; y < 48; y += 7)
		{
			for (int x = 0; x < 64; x += 7)
				CHECK(testing::pixel(picture, x, y).r == 0.0f);
		}
	}
}

TEST_CASE("a spot lights its cone, and its highlight is Blinn-Phong's", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	const double depth = 2.0;
	const View view = testing::view(96, 96, 60, 47.5, 47.5);
	Light spot;
	spot.kind = LightKind::Spot;
	spot.position = {0.0, 0.0, 0.0};
	spot.direction = {0.0, 0.0, 3.0}; // not unit length: the renderer normalises it
	spot.intensity = 2;
	spot.innerConeDegrees = 10;
	spot.outerConeDegrees = 20;

	Scene scene;
	scene.meshes = {testing::square()};
	Material material = plain({0.5f, 0.5f, 0.5f});
	material.specular = {0.25f, 0.25f, 0.25f};
	material.shininess = 20;
	scene.materials = {material};
	scene.instances = {Instance{0, math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, depth}), math::Vec3d{4.0}), {0}}};
	scene.lights = {spot};
	const image::Image picture = draw(renderer, scene, view);

	const double cosInner = std::cos(10 * 3.14159265358979323846 / 180);
	const double cosOuter = std::cos(20 * 3.14159265358979323846 / 180);
	int full = 0, dark = 0, between = 0;
	double worst = 0;
	for (int y = 0; y < 96; ++y)
	{
		for (int x = 0; x < 96; ++x)
		{
			const math::Vec3d p = onPlane(view, x, y, depth);
			const math::Vec3d l = math::normalize(spot.position - p);
			const double c = -l.z; // the angle from the spot's axis, and the angle of incidence
			const double t = std::clamp((c - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
			const double cone = t * t * (3 - 2 * t);
			// The camera and the light are both at the origin, so the half vector is the light's.
			const double unconed = (0.5 * c + 0.25 * std::pow(c, 20.0)) * spot.intensity / math::dot(p, p);
			const double actual = testing::pixel(picture, x, y).r;
			(cone == 1 ? full : cone == 0 ? dark
										  : between) += 1;
			// Against what the pixel would receive with no cone, since near the outer edge the light is
			// a small difference of cosines and a relative error there measures float, not the model.
			if (cone == 0)
				CHECK(actual == 0.0);
			else
				worst = std::max(worst, std::abs(actual - cone * unconed) / unconed);
		}
	}
	CHECK(full > 100);
	CHECK(dark > 100);
	CHECK(between > 100);
	CHECK(worst < 1e-4);
}

TEST_CASE("a texture is sampled where the mesh's coordinates say, in linear light", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	// Four 2 x 2 blocks of 8-bit sRGB: 128 decodes to 0.2158605, so a value that came back encoded
	// would read 0.502.
	auto texture = std::make_shared<image::Image>(4, 4, image::PixelFormat::RGBA8, image::ColorSpace::sRGB, image::AlphaMode::Straight);
	const std::array<std::array<std::uint8_t, 3>, 4> blocks = {{{128, 0, 0}, {0, 128, 0}, {0, 0, 128}, {128, 128, 128}}};
	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			const std::array<std::uint8_t, 3>& block = blocks[std::size_t((y / 2) * 2 + x / 2)];
			std::uint8_t* p = texture->data() + 4 * (y * 4 + x);
			p[0] = block[0];
			p[1] = block[1];
			p[2] = block[2];
			p[3] = 255;
		}
	}
	const double decoded = 0.2158605;

	Scene scene;
	scene.textures = {texture};
	Material material = plain({1.0f, 1.0f, 1.0f});
	material.texture = 0;
	scene.materials = {material};
	scene.ambient = {1.0f, 1.0f, 1.0f};
	// The square fills the middle of the view: 2 m across at 2 m through f = 32 is 32 px.
	const math::Mat4d placement = math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 2.0});
	const View view = testing::view(64, 64, 32, 31.5, 31.5);

	SECTION("upright, its top-left block at the top left")
	{
		scene.meshes = {testing::square()};
		scene.instances = {Instance{0, placement, {0}}};
		const image::Image picture = draw(renderer, scene, view);
		// Each block's middle is 8 px into its quarter of the square, which spans pixels 16 to 47.
		const image::ColorRGBAf topLeft = testing::pixel(picture, 23, 23);
		const image::ColorRGBAf topRight = testing::pixel(picture, 40, 23);
		const image::ColorRGBAf bottomLeft = testing::pixel(picture, 23, 40);
		const image::ColorRGBAf bottomRight = testing::pixel(picture, 40, 40);
		CHECK(std::abs(topLeft.r - decoded) < 1e-3);
		CHECK(topLeft.g == 0.0f);
		CHECK(std::abs(topRight.g - decoded) < 1e-3);
		CHECK(topRight.r == 0.0f);
		CHECK(std::abs(bottomLeft.b - decoded) < 1e-3);
		CHECK(bottomLeft.r == 0.0f);
		CHECK(std::abs(bottomRight.r - decoded) < 1e-3);
		CHECK(std::abs(bottomRight.b - decoded) < 1e-3);
	}
	SECTION("repeated where its coordinates run past 1")
	{
		scene.meshes = {testing::square(true, 2)};
		scene.instances = {Instance{0, placement, {0}}};
		const image::Image picture = draw(renderer, scene, view);
		// Twice across, so each block is 4 px: the top-left block of the bottom-right repeat sits at 34.
		const image::ColorRGBAf first = testing::pixel(picture, 18, 18);
		const image::ColorRGBAf repeat = testing::pixel(picture, 34, 34);
		CHECK(std::abs(first.r - decoded) < 1e-3);
		CHECK(std::abs(repeat.r - decoded) < 1e-3);
		CHECK(repeat.g == 0.0f);
	}
	SECTION("a linear texture is encoded once and decoded back")
	{
		auto grey = std::make_shared<image::Image>(4, 4, image::PixelFormat::Gray32F, image::ColorSpace::Linear);
		float* values = reinterpret_cast<float*>(grey->data());
		std::fill(values, values + 16, 0.3f);
		scene.textures = {grey};
		scene.meshes = {testing::square()};
		scene.instances = {Instance{0, placement, {0}}};
		const image::Image picture = draw(renderer, scene, view);
		const image::ColorRGBAf p = testing::pixel(picture, 31, 31);
		CHECK(std::abs(p.r - 0.3) < 0.004); // half an 8-bit sRGB step near 0.3
		CHECK(p.r == p.g);
		CHECK(p.g == p.b);
	}
}

TEST_CASE("the renderer refuses what it cannot draw, and says why", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	Scene broken;
	broken.instances = {Instance{0, math::Mat4d{1.0}, {}}};
	const UploadResult refused = renderer.upload(broken);
	CHECK_FALSE(refused.scene);
	CHECK(refused.problems.size() == 1);

	Scene empty;
	UploadResult uploaded = renderer.upload(empty);
	REQUIRE(uploaded.scene);
	View blind = testing::view(64, 48, 40, 31.5, 23.5);
	blind.intrinsics.fx = -1;
	const RenderResult unseen = renderer.render(*uploaded.scene, blind);
	CHECK_FALSE(unseen.image.valid());
	CHECK(unseen.problems.size() == 1);

	Renderer other(*device);
	const RenderResult stranger = other.render(*uploaded.scene, testing::view(64, 48, 40, 31.5, 23.5));
	CHECK_FALSE(stranger.image.valid());
	CHECK(stranger.problems.size() == 1);

	// Nothing in it draws the background, with no coverage.
	const RenderResult nothing = renderer.render(*uploaded.scene, testing::view(8, 8, 8, 3.5, 3.5));
	REQUIRE(nothing.image.valid());
	CHECK(testing::pixel(nothing.image, 3, 3).a == 0.0f);
}
