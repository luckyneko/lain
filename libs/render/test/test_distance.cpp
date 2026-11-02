// Integration: how far a view sees (Renderer::renderDistance), drawn on a real device and read back,
// each texel judged against the distance along its centre's ray to the squares the scene is made of,
// worked out here by intersecting that ray with each. SKIPs where the machine has no device.

#include "gpu.h"
#include "testscene.h"

#include <lain/render/renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

using namespace lain;
using namespace lain::render;

// The renderer under test, on each kind of device this machine offers (testing::device()).
#define REQUIRE_RENDERER(name)                \
	acm::Device* device = &testing::device(); \
	Renderer name(*device);                   \
	REQUIRE(name.problems().empty())

static constexpr double kPi = 3.14159265358979323846;

// A scene of testing::square()s, one instance each at these placements.
static Scene squares(const std::vector<math::Mat4d>& placements)
{
	Scene scene;
	scene.meshes = {testing::square()};
	scene.materials = {Material{}};
	for (const math::Mat4d& placement : placements)
		scene.instances.push_back(Instance{0, placement, {0}});
	return scene;
}

// The six squares of a box with these half extents, turned by `rotation` about its centre.
static std::vector<math::Mat4d> box(const math::Vec3d& centre, const math::Vec3d& half, const math::Mat4d& rotation)
{
	const math::Mat4d at = math::translate(math::Mat4d{1.0}, centre) * rotation;
	const auto face = [&](const math::Vec3d& offset, double angle, const math::Vec3d& axis, const math::Vec3d& size)
	{ return math::scale(math::rotate(math::translate(at, offset), angle, axis), size); };
	return {
		face({0.0, 0.0, -half.z}, 0.0, {0.0, 0.0, 1.0}, {half.x, half.y, 1.0}),
		face({0.0, 0.0, half.z}, 0.0, {0.0, 0.0, 1.0}, {half.x, half.y, 1.0}),
		face({-half.x, 0.0, 0.0}, kPi / 2, {0.0, 1.0, 0.0}, {half.z, half.y, 1.0}),
		face({half.x, 0.0, 0.0}, kPi / 2, {0.0, 1.0, 0.0}, {half.z, half.y, 1.0}),
		face({0.0, -half.y, 0.0}, kPi / 2, {1.0, 0.0, 0.0}, {half.x, half.z, 1.0}),
		face({0.0, half.y, 0.0}, kPi / 2, {1.0, 0.0, 0.0}, {half.x, half.z, 1.0}),
	};
}

// The ray through texel (u, v) of `view`: its centre and unit direction, in the world.
struct Ray
{
	math::Vec3d origin;
	math::Vec3d direction;
};

static Ray rayOf(const View& view, double u, double v)
{
	const camera::Intrinsics& k = view.intrinsics;
	const math::Vec3d d = math::normalize(math::Vec3d{(u - k.cx) / k.fx, (v - k.cy) / k.fy, 1.0});
	return {view.worldFromCamera.translation(), view.worldFromCamera.rotate(d)};
}

// How far along `ray` it meets the plane of the square at `placement`, whether within the square or
// not, or nothing when it runs parallel to it or meets it behind its origin.
static std::optional<double> toPlane(const math::Mat4d& placement, const Ray& ray, bool withinSquare)
{
	const math::Mat4d local = math::inverse(placement);
	const math::Vec3d o{local * math::Vec4d{ray.origin, 1.0}};
	const math::Vec3d d{local * math::Vec4d{ray.direction, 0.0}};
	if (std::abs(d.z) < 1e-12)
		return std::nullopt;
	const double t = -o.z / d.z; // a world distance, since the world direction is unit
	if (t <= 0)
		return std::nullopt;
	const math::Vec3d p = o + t * d;
	if (withinSquare && (std::abs(p.x) > 1 || std::abs(p.y) > 1))
		return std::nullopt;
	return t;
}

// The tangent of the angle between `ray` and the normal of the square at `placement`: how fast its
// distance along the ray changes as the ray turns, per radian, as a share of the distance.
static double slopeOf(const math::Mat4d& placement, const Ray& ray)
{
	const math::Vec3d n = math::normalize(math::transpose(math::inverse(math::Mat3d{placement})) * math::Vec3d{0.0, 0.0, 1.0});
	const double c = std::abs(math::dot(n, ray.direction));
	return std::sqrt(std::max(1 - c * c, 0.0)) / c;
}

// The nearest square `ray` meets: its index, and how far along the ray.
struct Hit
{
	std::optional<std::size_t> square;
	double distance = 0;
};

static Hit nearest(const std::vector<math::Mat4d>& placements, const Ray& ray)
{
	Hit hit;
	for (std::size_t i = 0; i < placements.size(); ++i)
	{
		const std::optional<double> t = toPlane(placements[i], ray, true);
		if (t && (!hit.square || *t < hit.distance))
			hit = {i, *t};
	}
	return hit;
}

// Whether texel (x, y) sees one square across all of its footprint (or none across all of it), so a
// rasteriser has nothing to decide there: the rays through its corners meet what its centre's does.
static bool settled(const std::vector<math::Mat4d>& placements, const View& view, int x, int y)
{
	const std::optional<std::size_t> centre = nearest(placements, rayOf(view, x, y)).square;
	for (const double du : {-0.5, 0.5})
	{
		for (const double dv : {-0.5, 0.5})
		{
			if (nearest(placements, rayOf(view, x + du, y + dv)).square != centre)
				return false;
		}
	}
	return true;
}

static float at(const image::Image& distances, int x, int y)
{
	return reinterpret_cast<const float*>(distances.data())[std::size_t(y) * distances.width() + x];
}

static image::Image distancesOf(Renderer& renderer, const Scene& scene, const View& view)
{
	UploadResult uploaded = renderer.upload(scene);
	REQUIRE(uploaded.problems.empty());
	REQUIRE(uploaded.scene);
	DistanceResult drawn = renderer.renderDistance(*uploaded.scene, view);
	REQUIRE(drawn.problems.empty());
	REQUIRE(drawn.image.valid());
	REQUIRE(drawn.image.pixelFormat() == image::PixelFormat::Gray32F);
	REQUIRE(drawn.image.width() == int(view.image.width));
	REQUIRE(drawn.image.height() == int(view.image.height));
	return std::move(drawn.image);
}

TEST_CASE("a texel's distance is along its centre's ray to what it meets, on a tilted plane and a box", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	// A box turned off every axis in front of a plane tilted both ways, seen by a camera that is not at
	// rest and whose principal point is not the image's centre. The box comes first, so drawn without a
	// depth test the plane would cover it.
	math::Mat4d turned = math::rotate(math::Mat4d{1.0}, 0.6, math::Vec3d{0.0, 1.0, 0.0});
	turned = math::rotate(turned, 0.35, math::Vec3d{1.0, 0.0, 0.0});
	std::vector<math::Mat4d> placements = box({-0.3, 0.15, 3.4}, {0.5, 0.4, 0.6}, turned);
	math::Mat4d plane = math::translate(math::Mat4d{1.0}, math::Vec3d{0.2, 0.1, 7.0});
	plane = math::rotate(plane, 0.5, math::Vec3d{0.0, 1.0, 0.0});
	plane = math::rotate(plane, -0.4, math::Vec3d{1.0, 0.0, 0.0});
	placements.push_back(math::scale(plane, math::Vec3d{5.0}));

	View view = testing::view(120, 90, 70, 57.3, 46.1);
	view.worldFromCamera = math::RigidTransformd{math::angleAxis(0.12, math::normalize(math::Vec3d{0.2, 1.0, -0.1})), {0.1, -0.2, -0.4}};
	const image::Image distances = distancesOf(renderer, squares(placements), view);

	// Off the image's axis a texel's distance is its depth over the cosine of its angle, up to 1.3 times
	// at these corners, so a pass that wrote depth would miss by a fifth. What a texel does miss by is the
	// rasteriser's: it places a vertex to 1/256 of a texel (both devices here; Vulkan's least is 1/16), so
	// the surface it draws is turned off the true one by up to half that angle, and a distance moves by
	// that angle times the surface's slope to the ray. Measured, the worst texel is 0.53 of that bound on
	// both devices, and the largest error 4.8e-5 of a distance, so the bound is the check.
	int onBox = 0, onPlane = 0, wrong = 0;
	double worst = 0; // of the bound
	for (int y = 0; y < 90; ++y)
	{
		for (int x = 0; x < 120; ++x)
		{
			if (!settled(placements, view, x, y))
				continue;
			const Hit hit = nearest(placements, rayOf(view, x, y));
			if (!hit.square)
			{
				wrong += at(distances, x, y) == kNoSurface ? 0 : 1; // past the plane's edge
				continue;
			}
			(*hit.square < 6 ? onBox : onPlane) += 1;
			const Ray ray = rayOf(view, x, y);
			const double error = std::abs(at(distances, x, y) - hit.distance) / hit.distance;
			const double bound = slopeOf(placements[*hit.square], ray) / (256 * view.intrinsics.fx) + 1e-6; // and float's own
			worst = std::max(worst, error / bound);
			if (error > bound)
				++wrong;
		}
	}
	INFO("the worst texel is " << worst << " of its bound off");
	CHECK(onBox > 400);
	CHECK(onPlane > 2000);
	CHECK(wrong == 0);
}

TEST_CASE("the nearest surface's distance is the one kept, whichever is drawn first", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	const math::Mat4d nearer = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 2.0}), math::Vec3d{0.5});
	const math::Mat4d farther = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 4.0}), math::Vec3d{3.0});
	const View view = testing::view(64, 64, 64, 31.5, 31.5);

	for (const std::vector<math::Mat4d>& order : {std::vector<math::Mat4d>{nearer, farther}, std::vector<math::Mat4d>{farther, nearer}})
	{
		const image::Image distances = distancesOf(renderer, squares(order), view);
		const double centre = *toPlane(nearer, rayOf(view, 32, 32), true);
		const double rim = *toPlane(farther, rayOf(view, 4, 32), true); // beyond the near square's edge
		CHECK(std::abs(at(distances, 32, 32) - centre) < 1e-5);
		CHECK(std::abs(at(distances, 4, 32) - rim) < 1e-5);
	}
}

TEST_CASE("where a texel's ray meets no surface between the near and far planes it reads kNoSurface", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	// A small square ahead, and one beyond the far plane (20 m), which is not drawn.
	const math::Mat4d ahead = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 3.0}), math::Vec3d{0.5});
	const math::Mat4d beyond = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 25.0}), math::Vec3d{40.0});
	const View view = testing::view(64, 48, 50, 31.5, 23.5);
	const image::Image distances = distancesOf(renderer, squares({ahead, beyond}), view);

	int nothing = 0, wrong = 0;
	for (int y = 0; y < 48; ++y)
	{
		for (int x = 0; x < 64; ++x)
		{
			if (!settled({ahead}, view, x, y) || nearest({ahead}, rayOf(view, x, y)).square)
				continue;
			++nothing;
			if (at(distances, x, y) != kNoSurface)
				++wrong;
		}
	}
	CHECK(nothing > 2000);
	CHECK(wrong == 0);
	CHECK(at(distances, 32, 24) < 3.1f);

	// A scene with nothing in it reads kNoSurface everywhere.
	const image::Image empty = distancesOf(renderer, Scene{}, testing::view(8, 8, 8, 3.5, 3.5));
	for (int y = 0; y < 8; ++y)
	{
		for (int x = 0; x < 8; ++x)
			CHECK(at(empty, x, y) == kNoSurface);
	}
}

TEST_CASE("a texel on a silhouette is one surface's distance, never a blend of two", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	// A square at about 2 m turned so its edges cross texels at every slant, over a plane at 6 m. A texel
	// its edge crosses holds one of the two distances along its centre's ray, never one between them: a
	// texel three quarters covered, resolved from four samples, would read 3 m, a metre from either.
	math::Mat4d nearer = math::translate(math::Mat4d{1.0}, math::Vec3d{0.1, -0.05, 2.0});
	nearer = math::rotate(nearer, 0.3, math::Vec3d{0.0, 0.0, 1.0});
	nearer = math::rotate(nearer, 0.25, math::Vec3d{0.0, 1.0, 0.0});
	nearer = math::scale(nearer, math::Vec3d{0.6});
	const math::Mat4d farther = math::scale(math::translate(math::Mat4d{1.0}, math::Vec3d{0.0, 0.0, 6.0}), math::Vec3d{10.0});
	const std::vector<math::Mat4d> placements{nearer, farther};
	const View view = testing::view(96, 72, 80, 47.3, 35.6);
	const image::Image distances = distancesOf(renderer, squares(placements), view);

	int crossed = 0, blended = 0;
	for (int y = 0; y < 72; ++y)
	{
		for (int x = 0; x < 96; ++x)
		{
			const Ray ray = rayOf(view, x, y);
			const double toNear = *toPlane(nearer, ray, false);
			const double toFar = *toPlane(farther, ray, false);
			const double value = at(distances, x, y);
			if (!settled(placements, view, x, y))
				++crossed;
			if (std::abs(value - toNear) > 1e-4 * toNear && std::abs(value - toFar) > 1e-4 * toFar)
				++blended;
		}
	}
	CHECK(crossed > 100);
	CHECK(blended == 0);
}

TEST_CASE("a distance the renderer cannot draw is refused, saying why", "[render][gpu]")
{
	REQUIRE_RENDERER(renderer);

	UploadResult uploaded = renderer.upload(Scene{});
	REQUIRE(uploaded.scene);
	View blind = testing::view(64, 48, 40, 31.5, 23.5);
	blind.intrinsics.fx = -1;
	const DistanceResult unseen = renderer.renderDistance(*uploaded.scene, blind);
	CHECK_FALSE(unseen.image.valid());
	CHECK(unseen.problems.size() == 1);

	Renderer other(*device);
	const DistanceResult stranger = other.renderDistance(*uploaded.scene, testing::view(64, 48, 40, 31.5, 23.5));
	CHECK_FALSE(stranger.image.valid());
	CHECK(stranger.problems.size() == 1);
}
