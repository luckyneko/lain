// Unit: a view's clip space against lain's own projection, and what makes a view undrawable. No
// device.

#include "testscene.h"

#include <lain/render/view.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace lain;
using namespace lain::render;

TEST_CASE("a view's clip space puts a point where lain's projection does", "[render][view]")
{
	View v;
	v.image = {640, 480};
	v.intrinsics = {500.0, 520.0, 319.2, 241.7};
	v.near = 0.1;
	v.far = 50;
	const camera::CameraModel model = testing::pinholeOf(v);
	const math::Mat4d clip = clipFromCamera(v);

	for (const math::Vec3d& point : {math::Vec3d{0.3, -0.2, 2.0}, math::Vec3d{-1.1, 0.7, 3.5}, math::Vec3d{0.0, 0.0, 0.1},
									 math::Vec3d{2.0, 1.5, 49.0}, math::Vec3d{-0.04, 0.03, 0.2}})
	{
		const math::Vec4d c = clip * math::Vec4d{point, 1.0};
		// Framebuffer x runs over [0, width); a pixel's centre is half a pixel in.
		const double framebufferX = (c.x / c.w + 1) / 2 * v.image.width;
		const double framebufferY = (c.y / c.w + 1) / 2 * v.image.height;
		const camera::Projection<double> projected = camera::project(model, point.x, point.y, point.z);
		REQUIRE(projected.ok());
		CHECK(framebufferX - 0.5 == Catch::Approx(projected.u).margin(1e-9));
		CHECK(framebufferY - 0.5 == Catch::Approx(projected.v).margin(1e-9));
	}
}

TEST_CASE("a view's depth runs from 0 at near to 1 at far", "[render][view]")
{
	View v = testing::view(64, 48, 40, 31.5, 23.5);
	v.near = 0.25;
	v.far = 12;
	const math::Mat4d clip = clipFromCamera(v);
	auto depth = [&](double z)
	{
		const math::Vec4d c = clip * math::Vec4d{0.0, 0.0, z, 1.0};
		return c.z / c.w;
	};
	CHECK(depth(0.25) == Catch::Approx(0.0).margin(1e-12));
	CHECK(depth(12) == Catch::Approx(1.0).margin(1e-12));
	CHECK(depth(1) < depth(2)); // nearer is smaller, so a Less test keeps the nearer
}

TEST_CASE("a view that cannot be drawn says why", "[render][view]")
{
	const View good = testing::view(64, 48, 40, 31.5, 23.5);
	CHECK(validate(good).empty());

	View empty = good;
	empty.image.height = 0;
	CHECK(validate(empty).size() == 1);

	View flat = good;
	flat.intrinsics.fy = 0;
	CHECK(validate(flat).size() == 1);

	View lost = good;
	lost.intrinsics.cx = std::numeric_limits<double>::quiet_NaN();
	CHECK(validate(lost).size() == 1);

	View inverted = good;
	inverted.near = 5;
	inverted.far = 1;
	CHECK(validate(inverted).size() == 1);

	View touching = good;
	touching.near = 0;
	CHECK(validate(touching).size() == 1);
}
