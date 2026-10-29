// lain's own triangulation (ADR-0016, "Producers propose; lain decides"): the point nearest to every
// ray, refused when the rays are too close to parallel for a depth, and reported when it lies behind
// a ray. Every tolerance is a measurement, written beside its check.

#include <lain/camera/feature/triangulation.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace lain;
using namespace lain::camera::feature;

namespace
{
	double angleBetween(const math::Vec3d& u, const math::Vec3d& v)
	{
		return std::atan2(math::length(math::cross(u, v)), math::dot(u, v));
	}
} // namespace

TEST_CASE("two rays meet where they cross", "[camera][feature][triangulation]")
{
	const math::Vec3d point{0.3, -0.2, 4.0};
	const math::Vec3d left{0.0}, right{1.0, 0.0, 0.0};
	const Triangulation t = triangulate({{left, point - left}, {right, 2.5 * (point - right)}}, 0.01);
	REQUIRE(t.ok());
	// Measured: 5e-14 m out.
	CHECK(math::length(t.point - point) < 1e-12);
	CHECK(std::abs(t.angle - angleBetween(point - left, point - right)) < 1e-15);
}

TEST_CASE("noisy rays land within their least-squares bound", "[camera][feature][triangulation]")
{
	// Five centres on an arc 4 m from the point, each ray turned up to 1 mrad off it at random. Each
	// ray then passes within 4 mm of the point, so the point is found within that.
	const math::Vec3d point{0.1, 0.05, 4.0};
	std::mt19937_64 engine(17);
	const auto uniform = [&engine]
	{ return double(engine() >> 11) * 0x1.0p-53 * 2.0 - 1.0; };
	std::vector<Ray> rays;
	for (int c = 0; c < 5; ++c)
	{
		const double a = 0.25 * (c - 2);
		const math::Vec3d centre{4.0 * std::sin(a), 0.0, 4.0 - 4.0 * std::cos(a)};
		const math::Vec3d towards = math::normalize(point - centre);
		const math::Vec3d axis = math::normalize(math::cross(towards, math::Vec3d{uniform(), uniform(), uniform()}));
		rays.push_back({centre, math::angleAxis(0.001 * uniform(), axis) * towards});
	}
	const Triangulation t = triangulate(rays, 0.01);
	REQUIRE(t.ok());
	// Measured: 1.6 mm out, and the parallax 1.0 rad (the arc's ends).
	CHECK(math::length(t.point - point) < 0.004);
	CHECK(t.angle > 0.99);
}

TEST_CASE("rays too close to parallel have no depth", "[camera][feature][triangulation]")
{
	const math::Vec3d left{0.0}, right{0.001, 0.0, 0.0};
	SECTION("below the minimum angle")
	{
		// A point 10 m away seen from 1 mm apart: 1e-4 rad of parallax.
		const math::Vec3d point{0.0, 0.0, 10.0};
		const Triangulation t = triangulate({{left, point - left}, {right, point - right}}, 0.001);
		CHECK(t.status == TriangulationStatus::IllConditioned);
		CHECK(std::abs(t.angle - 1e-4) < 1e-8);
		// The same rays with no minimum have a depth.
		CHECK(triangulate({{left, point - left}, {right, point - right}}, 0.0).ok());
	}
	SECTION("exactly parallel, with no minimum asked for")
	{
		const Triangulation t = triangulate({{left, {0.0, 0.0, 1.0}}, {right, {0.0, 0.0, 1.0}}}, 0.0);
		CHECK(t.status == TriangulationStatus::IllConditioned);
		CHECK(t.angle == 0.0);
	}
}

TEST_CASE("a point behind a ray's centre is reported, not returned as a depth", "[camera][feature][triangulation]")
{
	// Both lines pass through a point 4 m behind the first centre, whose ray points the other way.
	const math::Vec3d point{0.0, 0.0, -4.0};
	const math::Vec3d left{0.0}, right{1.0, 0.0, 0.0};
	const Triangulation t = triangulate({{left, {0.0, 0.0, 1.0}}, {right, point - right}}, 0.01);
	CHECK(t.status == TriangulationStatus::Behind);
	CHECK(math::length(t.point - point) < 1e-12);
}

TEST_CASE("fewer than two usable rays is too few", "[camera][feature][triangulation]")
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	CHECK(triangulate({{math::Vec3d{0.0}, {0.0, 0.0, 1.0}}}, 0.0).status == TriangulationStatus::TooFew);
	CHECK(triangulate({{math::Vec3d{0.0}, {0.0, 0.0, 1.0}}, {math::Vec3d{1.0, 0.0, 0.0}, math::Vec3d{0.0}}}, 0.0).status ==
		  TriangulationStatus::TooFew);
	CHECK(triangulate({{math::Vec3d{0.0}, {0.0, 0.0, 1.0}}, {math::Vec3d{nan, 0.0, 0.0}, {0.0, 0.0, 1.0}}}, 0.0).status ==
		  TriangulationStatus::TooFew);
}
