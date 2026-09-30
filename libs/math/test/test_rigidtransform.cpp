// Unit tests for lain::math::RigidTransform: composition order, inversion, and the matrix door that
// refuses anything that is not rigid.

#include "lain/math/rigidtransform.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lain::math;

namespace
{
	bool near(const Vec3d& a, const Vec3d& b, double tolerance = 1e-12)
	{
		return length(a - b) <= tolerance;
	}

	// 90 degrees about +Z, then a move along +X.
	RigidTransformd quarterTurnThenShift()
	{
		return RigidTransformd{angleAxis(std::acos(-1.0) / 2.0, Vec3d{0, 0, 1}), Vec3d{10, 0, 0}};
	}

	// A second, unrelated transform, so a composition test is not symmetric by accident.
	RigidTransformd tiltThenLift()
	{
		return RigidTransformd{angleAxis(0.3, normalize(Vec3d{1, 2, 3})), Vec3d{-1, 4, 2}};
	}
} // namespace

TEST_CASE("a default rigid transform is the identity", "[rigidtransform]")
{
	const RigidTransformd identity;
	REQUIRE(near(identity.apply(Vec3d{1, 2, 3}), Vec3d{1, 2, 3}));
	REQUIRE(near(identity.rotate(Vec3d{1, 2, 3}), Vec3d{1, 2, 3}));
}

TEST_CASE("a point is rotated, then translated", "[rigidtransform]")
{
	const RigidTransformd t = quarterTurnThenShift();
	REQUIRE(near(t.apply(Vec3d{1, 0, 0}), Vec3d{10, 1, 0}));
	// A direction has no position: only the rotation applies.
	REQUIRE(near(t.rotate(Vec3d{1, 0, 0}), Vec3d{0, 1, 0}));
}

TEST_CASE("composition chains the way aFromC = aFromB * bFromC reads", "[rigidtransform]")
{
	const RigidTransformd aFromB = quarterTurnThenShift();
	const RigidTransformd bFromC = tiltThenLift();
	const RigidTransformd aFromC = aFromB * bFromC;
	for (const Vec3d& p : {Vec3d{0, 0, 0}, Vec3d{1, -2, 5}, Vec3d{-3, 0.5, 2}})
		REQUIRE(near(aFromC.apply(p), aFromB.apply(bFromC.apply(p))));
	// And the other order is a different transform, so the test above could not pass by symmetry.
	REQUIRE_FALSE(near((bFromC * aFromB).apply(Vec3d{1, -2, 5}), aFromC.apply(Vec3d{1, -2, 5})));
}

TEST_CASE("the inverse undoes the transform from either side", "[rigidtransform]")
{
	const RigidTransformd t = tiltThenLift() * quarterTurnThenShift();
	const Vec3d p{1, -2, 5};
	REQUIRE(near(t.inverse().apply(t.apply(p)), p));
	REQUIRE(near(t.apply(t.inverse().apply(p)), p));
	REQUIRE(near((t * t.inverse()).apply(p), p));
}

TEST_CASE("the matrix form agrees with apply and comes back through fromMatrix", "[rigidtransform]")
{
	const RigidTransformd t = tiltThenLift() * quarterTurnThenShift();
	const Mat4d m = t.matrix();
	const Vec3d p{1, -2, 5};
	REQUIRE(near(Vec3d{m * Vec4d{p, 1.0}}, t.apply(p)));

	const auto back = RigidTransformd::fromMatrix(m);
	REQUIRE(back.has_value());
	REQUIRE(near(back->apply(p), t.apply(p), 1e-9));
}

TEST_CASE("fromMatrix refuses a matrix that is not rigid", "[rigidtransform]")
{
	REQUIRE(RigidTransformd::fromMatrix(Mat4d{1.0}).has_value());

	Mat4d scaled{1.0};
	scaled[0][0] = 2.0;
	REQUIRE_FALSE(RigidTransformd::fromMatrix(scaled).has_value());

	Mat4d sheared{1.0};
	sheared[1][0] = 0.25; // y's column leans into x
	REQUIRE_FALSE(RigidTransformd::fromMatrix(sheared).has_value());

	Mat4d reflected{1.0};
	reflected[2][2] = -1.0; // orthonormal, but a mirror: determinant -1
	REQUIRE_FALSE(RigidTransformd::fromMatrix(reflected).has_value());

	Mat4d projective{1.0};
	projective[2][3] = 0.1; // a non-zero bottom row entry
	REQUIRE_FALSE(RigidTransformd::fromMatrix(projective).has_value());
}
