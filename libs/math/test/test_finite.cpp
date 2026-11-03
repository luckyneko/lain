// lain::math::isFinite: a vector, a quaternion or a rigid transform holds only finite numbers.

#include "lain/math/finite.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace lain::math;

static const double kNaN = std::numeric_limits<double>::quiet_NaN();
static const double kInfinity = std::numeric_limits<double>::infinity();

TEST_CASE("a vector is finite when every component is", "[math][finite]")
{
	CHECK(isFinite(Vec2d{1.5, -2.0}));
	CHECK(isFinite(Vec3d{0.0}));
	CHECK(isFinite(Vec4f{1.0f, 2.0f, 3.0f, 4.0f}));
	CHECK(isFinite(Vec2i{3, -7}));
	// One bad component is enough, wherever it sits.
	for (int i = 0; i < 3; ++i)
	{
		Vec3d v{1.0, 2.0, 3.0};
		v[i] = kNaN;
		CHECK_FALSE(isFinite(v));
		v[i] = -kInfinity;
		CHECK_FALSE(isFinite(v));
	}
	CHECK_FALSE(isFinite(Vec4d{1.0, 2.0, 3.0, kInfinity}));
}

TEST_CASE("a quaternion is finite when every component is", "[math][finite]")
{
	CHECK(isFinite(Quatd{1, 0, 0, 0}));
	CHECK_FALSE(isFinite(Quatd{kNaN, 0, 0, 0}));
	CHECK_FALSE(isFinite(Quatd{1, 0, 0, kInfinity}));
}

TEST_CASE("a rigid transform is finite when its rotation and translation are", "[math][finite]")
{
	CHECK(isFinite(RigidTransformd{angleAxis(0.3, Vec3d{0, 0, 1}), Vec3d{1, 2, 3}}));
	CHECK_FALSE(isFinite(RigidTransformd{Quatd{1, 0, 0, 0}, Vec3d{1, kNaN, 3}}));
	// An infinite rotation passes the constructor's non-zero check and normalises to NaN.
	CHECK_FALSE(isFinite(RigidTransformd{Quatd{kInfinity, 0, 0, 0}, Vec3d{1, 2, 3}}));
}
