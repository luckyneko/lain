// Integration: capture renders (ADR-0028) drawn on a real device and read back, each judged against
// what lain's own camera geometry and the shading model say the picture must be: the ray table's
// own sub-sample rays cast on the CPU, never stored pixels. SKIPs where the machine has no device.

#include "gpu.h"
#include "testcameras.h"
#include "testscene.h"

#include <lain/render/capture.h>
#include <lain/render/renderer.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

using namespace lain;
using namespace lain::render;

#define REQUIRE_RENDERER(name)                \
	acm::Device* device = &testing::device(); \
	Renderer name(*device);                   \
	REQUIRE(name.problems().empty())

static camera::CameraModel modelOf(const camera::CameraModelParameters& parameters)
{
	const camera::ModelResult result = camera::CameraModel::create(parameters);
	REQUIRE(result.model.has_value());
	return *result.model;
}

static RayTable tableOf(const camera::CameraModel& model, const CaptureSettings& settings = {})
{
	RayTableResult result = RayTable::build(model, settings);
	REQUIRE(result.table.has_value());
	return std::move(*result.table);
}

// `scene` as `table`'s camera records it from `view`.
static image::Image capture(Renderer& renderer, const Scene& scene, const RayTable& table, const CaptureView& view = {})
{
	UploadResult uploaded = renderer.upload(scene);
	REQUIRE(uploaded.problems.empty());
	GpuRayTableResult rays = renderer.upload(table);
	REQUIRE(rays.problems.empty());
	RenderResult captured = renderer.capture(*uploaded.scene, *rays.table, view);
	for (const std::string& problem : captured.problems)
		UNSCOPED_INFO(problem);
	REQUIRE(captured.problems.empty());
	REQUIRE(captured.image.valid());
	return std::move(captured.image);
}

// A square mesh placed by columns: its X across `b`, its Y across `c`, its face toward `a`, all in the
// world, at `centre`. testscene.h's square() is two metres across, so `b` and `c` are half-sides.
static Instance placed(const math::Vec3d& centre, const math::Vec3d& b, const math::Vec3d& c, const math::Vec3d& a)
{
	Instance instance;
	instance.mesh = 0;
	instance.materials = {0};
	instance.worldFromMesh = math::Mat4d{math::Vec4d{b, 0.0}, math::Vec4d{c, 0.0}, math::Vec4d{a, 0.0}, math::Vec4d{centre, 1.0}};
	return instance;
}

// A point light, an ambient light and a matte white material: what every lit case shades with.
struct Lighting
{
	math::Vec3d light{0.4, -0.3, 1.2};
	double radiance = 2.0;
	double ambient = 0.05;

	// What a surface with normal `n` at `p`, seen from the origin, sends back: scene.frag's model.
	double at(const math::Vec3d& p, math::Vec3d n) const
	{
		if (math::dot(n, -p) < 0)
			n = -n; // lit from whichever side the camera sees
		const math::Vec3d toLight = light - p;
		const double distanceSquared = math::dot(toLight, toLight);
		const double lambert = math::dot(n, toLight / std::sqrt(distanceSquared));
		return ambient + (lambert > 0 ? radiance * lambert / distanceSquared : 0.0);
	}

	void into(Scene& scene) const
	{
		Light point;
		point.position = light;
		point.intensity = radiance;
		scene.lights = {point};
		scene.ambient = image::ColorRGBf{float(ambient), float(ambient), float(ambient)};
	}
};

static Material white()
{
	Material material;
	material.diffuse = image::ColorRGBf{1.0f, 1.0f, 1.0f};
	return material;
}

// A wide plane `depth` ahead of the camera, facing it, wider than any of the test cameras sees.
static Scene litPlane(const Lighting& lighting, double depth = 2)
{
	Scene scene;
	scene.meshes = {testing::square()};
	scene.materials = {white()};
	scene.instances = {placed({0.0, 0.0, depth}, {100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}, {0.0, 0.0, 1.0})};
	lighting.into(scene);
	return scene;
}

// A box room of half-size `half` about the camera, turned by `turn`, every wall matte white.
static Scene boxRoom(const Lighting& lighting, double half, const math::Mat3d& turn)
{
	Scene scene;
	scene.meshes = {testing::square()};
	scene.materials = {white()};
	for (int axis = 0; axis < 3; ++axis)
	{
		for (double sign : {-1.0, 1.0})
		{
			math::Vec3d a{0.0}, b{0.0}, c{0.0};
			a[axis] = sign;
			b[(axis + 1) % 3] = 1;
			c[(axis + 2) % 3] = 1;
			scene.instances.push_back(placed(turn * (half * a), turn * (half * b), turn * (half * c), turn * a));
		}
	}
	lighting.into(scene);
	return scene;
}

// For every pixel of `table`'s camera, the mean over its rays of what `ray(d)` says a ray sees, and
// which surface they met, for telling creases apart.
struct Expected
{
	std::vector<double> value;
	std::vector<int> surface; // the surface the pixel's rays met, or -1 when they met more than one
};

static Expected expect(const RayTable& table, const std::function<std::pair<double, int>(const math::Vec3d&)>& ray)
{
	const int w = int(table.model().image().width), h = int(table.model().image().height), s = int(table.settings().samples);
	Expected expected{std::vector<double>(std::size_t(w) * h, 0.0), std::vector<int>(std::size_t(w) * h, -1)};
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			double sum = 0;
			int surface = -1;
			bool crease = false;
			for (int j = 0; j < s; ++j)
			{
				for (int k = 0; k < s; ++k)
				{
					const auto [value, id] = ray(table.direction(x + (k + 0.5) / s - 0.5, y + (j + 0.5) / s - 0.5));
					sum += value;
					crease = crease || (surface >= 0 && id != surface);
					surface = id;
				}
			}
			expected.value[std::size_t(y) * w + x] = sum / (s * s);
			expected.surface[std::size_t(y) * w + x] = crease ? -1 : surface;
		}
	}
	return expected;
}

// The worst relative difference between `picture`'s red channel and `expected`, over the pixels no
// crease is within a pixel of: whose rays, and their neighbours', all met one surface. A crease can
// pass between two pixels' rays, so a pixel's own rays alone do not find it.
static double worstRelative(const image::Image& picture, const Expected& expected)
{
	const int w = picture.width(), h = picture.height();
	double worst = 0;
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const int surface = expected.surface[std::size_t(y) * w + x];
			bool nearCrease = surface < 0;
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int nx = std::clamp(x + dx, 0, w - 1), ny = std::clamp(y + dy, 0, h - 1);
					nearCrease = nearCrease || expected.surface[std::size_t(ny) * w + nx] != surface;
				}
			}
			if (nearCrease)
				continue;
			const double want = expected.value[std::size_t(y) * w + x];
			worst = std::max(worst, std::abs(testing::pixel(picture, x, y).r - want) / want);
		}
	}
	return worst;
}

TEST_CASE("a capture through a pinhole at one ray a pixel is what the pinhole renders", "[render][capture][gpu]")
{
	// The remap's texel convention, bilinear weights and front face, held against the renderer's own
	// pinhole: where the picture is smooth they must agree, a half texel's slip apart showing as the
	// light's gradient.
	REQUIRE_RENDERER(renderer);
	const camera::CameraModel model = modelOf(testing::strongCamera(0, 0.25));
	CaptureSettings settings;
	settings.samples = 1;
	const RayTable table = tableOf(model, settings);
	const Scene scene = litPlane(Lighting{});
	const image::Image captured = capture(renderer, scene, table);

	View view;
	view.image = model.image();
	view.intrinsics = model.intrinsics();
	UploadResult uploaded = renderer.upload(scene);
	REQUIRE(uploaded.scene);
	const RenderResult rendered = renderer.render(*uploaded.scene, view);
	REQUIRE(rendered.problems.empty());

	double worst = 0;
	for (int y = 0; y < captured.height(); ++y)
	{
		for (int x = 0; x < captured.width(); ++x)
		{
			const double want = testing::pixel(rendered.image, x, y).r;
			worst = std::max(worst, std::abs(testing::pixel(captured, x, y).r - want) / want);
		}
	}
	// Measured: 9.8e-5 and 1.1e-4 on the two devices, the bilinear blend of a curved light between
	// texels half a pixel apart.
	INFO("worst relative difference: " << worst);
	CHECK(worst < 3e-4);
}

TEST_CASE("a lit plane through every model is the shading where each of its rays meets it", "[render][capture][gpu]")
{
	REQUIRE_RENDERER(renderer);
	const Lighting lighting;
	const Scene scene = litPlane(lighting);
	for (int i = 0; i < testing::kStrongModelCount; ++i)
	{
		const RayTable table = tableOf(modelOf(testing::strongCamera(i, 0.25)));
		const image::Image captured = capture(renderer, scene, table);
		const Expected expected = expect(table,
										 [&](const math::Vec3d& d)
										 { return std::pair{lighting.at(d * (2 / d.z), {0.0, 0.0, -1.0}), 0}; });
		const double worst = worstRelative(captured, expected);
		// Measured: 3.4e-5 (inverse Brown-Conrady) to 1.3e-4 (the fisheye), on both devices.
		INFO(camera::displayName(table.model().distortion()) << ": worst relative difference " << worst);
		CHECK(worst < 3e-4);
		std::size_t uncovered = 0;
		for (int y = 0; y < captured.height(); ++y)
		{
			for (int x = 0; x < captured.width(); ++x)
				uncovered += testing::pixel(captured, x, y).a != 1.0f;
		}
		CHECK(uncovered == 0); // every ray met the plane, so every pixel is covered exactly
	}
}

TEST_CASE("a fisheye in a lit room shows no seam where the intermediate image's faces meet", "[render][capture][gpu]")
{
	// The room is turned against the camera, so its creases cross the cube's seams rather than lying
	// on them: every seam runs through the middle of a wall, where the picture must be smooth.
	REQUIRE_RENDERER(renderer);
	const Lighting lighting{{0.3, -0.2, 0.1}, 3.0, 0.05};
	const math::Mat3d turn = math::mat3_cast(math::angleAxis(0.35, math::Vec3d{0, 1, 0}) * math::angleAxis(0.2, math::Vec3d{1, 0, 0}));
	const double half = 2;
	const Scene scene = boxRoom(lighting, half, turn);
	for (std::uint32_t side : {std::uint32_t{4096}, std::uint32_t{64}})
	{
		CaptureSettings settings;
		settings.maxTileSide = side;
		const RayTable table = tableOf(modelOf(testing::strongCamera(5, 0.25)), settings);
		REQUIRE(table.cube());
		const image::Image captured = capture(renderer, scene, table);
		const Expected expected = expect(table,
										 [&](const math::Vec3d& d)
										 {
											 const math::Vec3d inBox = math::transpose(turn) * d;
											 int axis = 0;
											 for (int a = 1; a < 3; ++a)
											 {
												 if (std::abs(inBox[a]) > std::abs(inBox[axis]))
													 axis = a;
											 }
											 math::Vec3d normal{0.0};
											 normal[axis] = inBox[axis] > 0 ? -1 : 1;
											 const math::Vec3d hit = d * (half / std::abs(inBox[axis]));
											 return std::pair{lighting.at(hit, turn * normal), 2 * axis + (inBox[axis] > 0)};
										 });
		const double worst = worstRelative(captured, expected);
		// Measured: 3.1e-5 to 3.6e-5, the same whether its five faces are five tiles or 102.
		INFO("tiles at most " << side << " a side (" << table.tiles().size() << "): worst relative difference " << worst);
		CHECK(worst < 2e-4);
	}
}

TEST_CASE("a capture draws everything farther than its near plane, at any angle off a face's axis", "[render][capture][gpu]")
{
	// A room nearer than the near plane in the middle of each wall and farther toward its corners. A face
	// measures depth along its own axis, so what lies 54 degrees off it is only 0.58 of its distance
	// deep there: drawn with the capture's near plane unscaled, it would be cut away.
	REQUIRE_RENDERER(renderer);
	const Lighting lighting{{0.1, 0.1, 0.1}, 1.0, 0.5};
	const math::Mat3d turn = math::mat3_cast(math::angleAxis(0.35, math::Vec3d{0, 1, 0}));
	const double half = 0.9;
	const RayTable table = tableOf(modelOf(testing::strongCamera(5, 0.25)));
	CaptureView view;
	view.nearPlane = 1.0;
	const image::Image captured = capture(renderer, boxRoom(lighting, half, turn), table, view);

	const int w = captured.width(), h = captured.height(), s = int(table.settings().samples);
	std::size_t far = 0, missing = 0;
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			double nearest = std::numeric_limits<double>::infinity();
			for (int j = 0; j < s; ++j)
			{
				for (int k = 0; k < s; ++k)
				{
					const math::Vec3d inBox = math::transpose(turn) * table.direction(x + (k + 0.5) / s - 0.5, y + (j + 0.5) / s - 0.5);
					nearest = std::min(nearest, half / std::max({std::abs(inBox.x), std::abs(inBox.y), std::abs(inBox.z)}));
				}
			}
			if (nearest < 1.05)
				continue;
			++far;
			missing += testing::pixel(captured, x, y).a != 1.0f;
		}
	}
	INFO(far << " pixels see only walls farther than 1.05 m");
	CHECK(far > 1000);
	CHECK(missing == 0);
}

TEST_CASE("a square's coverage is exact wherever its edges are more than a texel away", "[render][capture][gpu]")
{
	// A square 1 m across, 2 m ahead, on a background that is not black: away from its edges a pixel's
	// rays are all on it or all off it, and its light and coverage are exactly the surface's or exactly
	// the background's. At its edges the face pass's single sample a texel places the edge to half a
	// texel, which is measured.
	REQUIRE_RENDERER(renderer);
	Scene scene;
	scene.meshes = {testing::square()};
	scene.materials = {white()};
	scene.instances = {placed({0.1, -0.05, 2.0}, {0.5, 0.0, 0.0}, {0.0, 0.5, 0.0}, {0.0, 0.0, 1.0})};
	scene.ambient = image::ColorRGBf{1.0f, 1.0f, 1.0f};
	scene.background = image::ColorRGBf{0.25f, 0.5f, 0.75f};
	for (int i = 0; i < testing::kStrongModelCount - 1; ++i) // the front-face cameras: the square is one convex shape on it
	{
		const RayTable table = tableOf(modelOf(testing::strongCamera(i, 0.25)));
		REQUIRE_FALSE(table.cube());
		const image::Image captured = capture(renderer, scene, table);

		// The square's corners on the face, where its edges are straight.
		std::array<math::Vec2d, 4> corner;
		const std::array<math::Vec3d, 4> world = {math::Vec3d{-0.4, -0.55, 2.0}, math::Vec3d{0.6, -0.55, 2.0}, math::Vec3d{0.6, 0.45, 2.0},
												  math::Vec3d{-0.4, 0.45, 2.0}};
		for (std::size_t c = 0; c < 4; ++c)
			corner[c] = table.onFace(0, world[c]);
		// How far inside the square (positive) a face coordinate is, in texels.
		const auto inside = [&](const math::Vec2d& q)
		{
			double least = std::numeric_limits<double>::infinity();
			for (std::size_t c = 0; c < 4; ++c)
			{
				const math::Vec2d a = corner[c], b = corner[(c + 1) % 4], e = math::normalize(b - a);
				least = std::min(least, (q - a).y * e.x - (q - a).x * e.y);
			}
			return least;
		};

		const int w = captured.width(), h = captured.height(), s = int(table.settings().samples);
		std::size_t clear = 0, wrong = 0, edges = 0;
		double worstEdge = 0;
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				double least = std::numeric_limits<double>::infinity(), coverage = 0;
				for (int j = 0; j < s; ++j)
				{
					for (int k = 0; k < s; ++k)
					{
						const double depth = inside(table.onFace(0, table.direction(x + (k + 0.5) / s - 0.5, y + (j + 0.5) / s - 0.5)));
						least = std::min(least, std::abs(depth));
						coverage += depth > 0 ? 1.0 / (s * s) : 0.0;
					}
				}
				const image::ColorRGBAf p = testing::pixel(captured, x, y);
				if (least > 2)
				{
					++clear;
					const bool on = coverage == 1.0;
					const bool exact = on ? (p.r == 1.0f && p.b == 1.0f && p.a == 1.0f) : (p.r == 0.25f && p.b == 0.75f && p.a == 0.0f);
					wrong += !exact;
				}
				else
				{
					++edges;
					worstEdge = std::max(worstEdge, std::abs(double(p.a) - coverage));
				}
			}
		}
		INFO(camera::displayName(table.model().distortion()) << ": " << clear << " pixels clear of the edges, " << edges
															 << " at them, worst coverage difference there " << worstEdge);
		CHECK(clear > 10000);
		CHECK(wrong == 0);
		// Measured: 0.13 (Brown-Conrady) to 0.34 (modified Brown-Conrady), on both devices: an edge placed
		// to half a texel, a quarter of a pixel at m = 2, plus a sixteenth for the rays.
		CHECK(worstEdge < 0.5);
	}
}

TEST_CASE("a capture tiled finely is the capture tiled coarsely", "[render][capture][gpu]")
{
	REQUIRE_RENDERER(renderer);
	const Scene scene = litPlane(Lighting{});
	for (int i : {1, 5})
	{
		const camera::CameraModel model = modelOf(testing::strongCamera(i, 0.25));
		CaptureSettings fine;
		fine.maxTileSide = 64;
		const RayTable coarseTable = tableOf(model);
		const RayTable fineTable = tableOf(model, fine);
		REQUIRE(fineTable.tiles().size() > coarseTable.tiles().size());
		const image::Image coarse = capture(renderer, scene, coarseTable);
		const image::Image tiled = capture(renderer, scene, fineTable);
		double worst = 0;
		for (int y = 0; y < coarse.height(); ++y)
		{
			for (int x = 0; x < coarse.width(); ++x)
			{
				const double a = testing::pixel(coarse, x, y).r, b = testing::pixel(tiled, x, y).r;
				worst = std::max(worst, std::abs(a - b) / a);
			}
		}
		// Measured: 1.4e-5 to 4.8e-5. Each tile draws the plane's two triangles, 200 m across, through
		// its own clip mapping, so the float interpolation of where a texel lands differs a little.
		INFO(camera::displayName(model.distortion()) << ": " << fineTable.tiles().size() << " tiles against "
													 << coarseTable.tiles().size() << ", worst relative difference " << worst);
		CHECK(worst < 1e-4);
	}
}

TEST_CASE("a capture refuses a table or scene another renderer uploaded, and a view it cannot draw", "[render][capture][gpu]")
{
	REQUIRE_RENDERER(renderer);
	Renderer other(*device);
	REQUIRE(other.problems().empty());
	const RayTable table = tableOf(modelOf(testing::strongCamera(0, 0.125)));
	const Scene scene = litPlane(Lighting{});
	UploadResult mine = renderer.upload(scene);
	UploadResult theirs = other.upload(scene);
	GpuRayTableResult myTable = renderer.upload(table);
	GpuRayTableResult theirTable = other.upload(table);
	REQUIRE((mine.scene && theirs.scene && myTable.table && theirTable.table));

	CHECK(renderer.capture(*mine.scene, *myTable.table, {}).problems.empty());
	const auto refusal = [](const RenderResult& result)
	{
		CHECK_FALSE(result.image.valid());
		REQUIRE(result.problems.size() == 1);
		return result.problems[0];
	};
	CHECK(refusal(renderer.capture(*theirs.scene, *myTable.table, {})).find("scene") != std::string::npos);
	CHECK(refusal(renderer.capture(*mine.scene, *theirTable.table, {})).find("ray table") != std::string::npos);
	CaptureView view;
	view.nearPlane = 0;
	CHECK(refusal(renderer.capture(*mine.scene, *myTable.table, view)).find("depth range") != std::string::npos);
	view = {};
	view.worldFromCamera = math::RigidTransformd{math::Quatd{1, 0, 0, 0}, math::Vec3d{std::nan(""), 0, 0}};
	CHECK(refusal(renderer.capture(*mine.scene, *myTable.table, view)).find("pose") != std::string::npos);
}
