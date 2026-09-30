// Unit tests for lain::math::AxisConvention and basisChange.

#include "lain/math/axisconvention.h"

#include <catch2/catch_test_macros.hpp>

using namespace lain::math;
using D = AxisDirection;

namespace
{
	AxisConvention convention(D x, D y, D z)
	{
		const auto made = AxisConvention::create(x, y, z);
		REQUIRE(made.has_value());
		return *made;
	}

	bool equal(const Mat3d& a, const Mat3d& b)
	{
		for (length_t c = 0; c < 3; ++c)
		{
			if (a[c] != b[c])
				return false;
		}
		return true;
	}
} // namespace

TEST_CASE("a convention needs three different physical axes", "[axisconvention]")
{
	REQUIRE_FALSE(AxisConvention::create(D::Right, D::Left, D::Up).has_value());
	REQUIRE_FALSE(AxisConvention::create(D::Up, D::Down, D::Forward).has_value());
	REQUIRE_FALSE(AxisConvention::create(D::Forward, D::Up, D::Backward).has_value());
	REQUIRE(AxisConvention::create(D::Forward, D::Left, D::Up).has_value());
	REQUIRE(AxisConvention::XRightYDownZForward().toString() == "XRightYDownZForward");
	REQUIRE(convention(D::Forward, D::Left, D::Up).toString() == "XForwardYLeftZUp");
}

TEST_CASE("handedness is measured physically", "[axisconvention]")
{
	REQUIRE(AxisConvention::XRightYDownZForward().rightHanded()); // the camera frame
	REQUIRE(AxisConvention::XRightYUpZBackward().rightHanded());
	REQUIRE(convention(D::Forward, D::Left, D::Up).rightHanded()); // a Z-up vehicle frame
	REQUIRE_FALSE(convention(D::Right, D::Up, D::Forward).rightHanded());
	REQUIRE_FALSE(convention(D::Down, D::Right, D::Forward).rightHanded());
}

TEST_CASE("basisChange re-expresses a point in the other convention", "[axisconvention]")
{
	const AxisConvention camera = AxisConvention::XRightYDownZForward();
	const AxisConvention graphics = AxisConvention::XRightYUpZBackward();

	const Mat3d cameraToGraphics = basisChange<double>(camera, graphics);
	REQUIRE(equal(cameraToGraphics, Mat3d{Vec3d{1, 0, 0}, Vec3d{0, -1, 0}, Vec3d{0, 0, -1}}));
	REQUIRE(cameraToGraphics * Vec3d{0, 0, 1} == Vec3d{0, 0, -1}); // straight ahead
	REQUIRE(cameraToGraphics * Vec3d{0, -1, 0} == Vec3d{0, 1, 0}); // straight up

	// Not symmetric, unlike camera <-> graphics, so this pins the DIRECTION of the change: a vehicle
	// frame's forward (its +X) is the camera's +Z.
	const Mat3d vehicleToCamera = basisChange<double>(convention(D::Forward, D::Left, D::Up), camera);
	REQUIRE(vehicleToCamera * Vec3d{1, 0, 0} == Vec3d{0, 0, 1});
	REQUIRE(vehicleToCamera * Vec3d{0, 1, 0} == Vec3d{-1, 0, 0}); // left
	REQUIRE(vehicleToCamera * Vec3d{0, 0, 1} == Vec3d{0, -1, 0}); // up
}

TEST_CASE("a basis change is exact, and undone by the reverse change", "[axisconvention]")
{
	const AxisConvention conventions[] = {AxisConvention::XRightYDownZForward(), AxisConvention::XRightYUpZBackward(),
										  convention(D::Forward, D::Left, D::Up), convention(D::Right, D::Up, D::Forward)};
	for (const AxisConvention& a : conventions)
	{
		REQUIRE(equal(basisChange<double>(a, a), Mat3d{1.0}));
		for (const AxisConvention& b : conventions)
		{
			INFO(a.toString() << " -> " << b.toString());
			REQUIRE(equal(basisChange<double>(b, a) * basisChange<double>(a, b), Mat3d{1.0}));
			const double expected = a.rightHanded() == b.rightHanded() ? 1.0 : -1.0;
			REQUIRE(determinant(basisChange<double>(a, b)) == expected);
		}
	}
}
