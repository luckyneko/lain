// Unit tests for lain::math's angles (angles.h) and the GLM constants it re-exposes: the angle between
// two directions and between two rotations, held to the precision near zero that an acos form loses.

#include "lain/math/angles.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lain::math;

TEST_CASE("the constants are the values the hand-typed digits were", "[math][angles]")
{
	// What lets every copy of these digits be replaced without moving a bit.
	CHECK(pi<double>() == 3.141592653589793);
	CHECK(two_pi<double>() == 6.283185307179586);
	CHECK(two_pi<float>() == 6.2831853f);
}

TEST_CASE("angleBetween measures the angle between two directions", "[math][angles]")
{
	const Vec3d x{1, 0, 0};
	CHECK(angleBetween(x, x) == 0.0);
	CHECK(std::abs(angleBetween(x, Vec3d{0, 2, 0}) - pi<double>() / 2) < 1e-15);
	CHECK(std::abs(angleBetween(x, Vec3d{-3, 0, 0}) - pi<double>()) < 1e-15);
	// Neither need be unit: scaling either leaves the angle.
	const Vec3d u{0.3, -1.2, 2.0}, v{-0.7, 0.4, 1.1};
	CHECK(std::abs(angleBetween(5.0 * u, 0.25 * v) - angleBetween(u, v)) < 1e-15);
}

TEST_CASE("angleBetween keeps its precision for tiny angles", "[math][angles]")
{
	// acos of their cosine reads 0: the cosine of 1e-10 rounds to exactly 1.
	for (const double angle : {1e-10, 1e-12, 3e-9})
	{
		const Vec3d v{std::cos(angle), std::sin(angle), 0};
		CHECK(std::abs(angleBetween(Vec3d{1, 0, 0}, v) - angle) <= 1e-6 * angle);
	}
}

TEST_CASE("rotationBetween measures the rotation taking one orientation to another", "[math][angles]")
{
	const Vec3d axis = normalize(Vec3d{1, 2, 3});
	const Quatd a = angleAxis(0.5, axis);
	CHECK(std::abs(rotationBetween(a, angleAxis(2.5, axis)) - 2.0) < 1e-14);
	// The order does not matter: b to a is a to b undone.
	const Quatd b = angleAxis(1.3, normalize(Vec3d{-2, 0, 1}));
	CHECK(std::abs(rotationBetween(a, b) - rotationBetween(b, a)) < 1e-15);

	SECTION("q and -q are one rotation, and the answer is the shorter way round")
	{
		CHECK(rotationBetween(a, -a) == 0.0);
		CHECK(std::abs(rotationBetween(Quatd{1, 0, 0, 0}, angleAxis(4.0, axis)) - (two_pi<double>() - 4.0)) < 1e-14);
	}
	SECTION("neither need be unit")
	{
		CHECK(std::abs(rotationBetween(3.0 * a, 0.5 * b) - rotationBetween(a, b)) < 1e-14);
	}
	SECTION("a transform's translation plays no part")
	{
		const RigidTransformd ta{a, Vec3d{1, 2, 3}}, tb{angleAxis(2.5, axis), Vec3d{-7, 0, 4}};
		const RigidTransformd unmovedA{a, Vec3d{0.0}}, unmovedB{angleAxis(2.5, axis), Vec3d{0.0}};
		CHECK(rotationBetween(ta, tb) == rotationBetween(unmovedA, unmovedB));
		CHECK(std::abs(rotationBetween(ta, tb) - 2.0) < 1e-14);
	}
}

TEST_CASE("rotationBetween keeps its precision for tiny rotations", "[math][angles]")
{
	// 2 acos of the quaternions' dot product reads 0 for each of these, and can tell nothing below
	// about 3e-8 rad from 0. From the identity, so the relative quaternion is the turn itself and the
	// only rounding is the formula's.
	const Vec3d axis = normalize(Vec3d{0.2, -1, 0.5});
	for (const double angle : {1e-10, 1e-12, 3e-9})
		CHECK(std::abs(rotationBetween(Quatd{1, 0, 0, 0}, angleAxis(angle, axis)) - angle) <= 1e-6 * angle);
}
