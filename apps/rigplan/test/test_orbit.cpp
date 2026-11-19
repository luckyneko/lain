// The Viewport's orbit (M15 slice 8d): a turntable that keeps +Z up, and a framing that puts all of a box
// in view. Judged through lain's own pinhole projection of the view it makes.

#include "orbit.h"

#include <lain/math/types.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace math = lain::math;
using Catch::Approx;

static const lain::camera::ImageGeometry kPane{960, 540};

// Where `view`'s pinhole puts plan point `p`, in pixels, and its depth.
static math::Vec3d project(const lain::render::View& view, const math::Vec3d& p)
{
	const math::Vec3d c = view.worldFromCamera.inverse().apply(p);
	const lain::camera::Intrinsics& k = view.intrinsics;
	return {k.fx * c.x / c.z + k.cx, k.fy * c.y / c.z + k.cy, c.z};
}

TEST_CASE("the orbit keeps +Z up, and looks at its target, however it is turned", "[rigplan][orbit]")
{
	for (double yaw = -180.0; yaw <= 180.0; yaw += 22.5)
	{
		for (double pitch = -85.0; pitch <= 85.0; pitch += 15.0) // never level, where eye and target are at one height
		{
			rigplan::Orbit orbit;
			orbit.target = {1.0, -2.0, 0.8};
			orbit.yawDegrees = yaw;
			orbit.pitchDegrees = pitch;
			const lain::render::View view = rigplan::viewOf(orbit, kPane);
			INFO("yaw " << yaw << " pitch " << pitch);
			// The image's right is level, its up has +Z in it, and the target is at its centre.
			CHECK(view.worldFromCamera.rotate({1.0, 0.0, 0.0}).z == Approx(0.0).margin(1e-12));
			CHECK(view.worldFromCamera.rotate({0.0, -1.0, 0.0}).z > 0.0);
			const math::Vec3d target = project(view, orbit.target);
			CHECK(target.x == Approx(view.intrinsics.cx).margin(1e-9));
			CHECK(target.y == Approx(view.intrinsics.cy).margin(1e-9));
			CHECK(target.z == Approx(orbit.distance));
			// A positive pitch stands it above the target, looking down.
			CHECK((view.worldFromCamera.translation().z > orbit.target.z) == (pitch > 0.0));
		}
	}

	// At rest it looks along +Y, as a camera at rest does, and a pan turns it left.
	rigplan::Orbit level;
	level.pitchDegrees = 0;
	CHECK(rigplan::viewOf(level, kPane).worldFromCamera.rotate({0.0, 0.0, 1.0}).y == Approx(1.0));
	level.yawDegrees = 90;
	CHECK(rigplan::viewOf(level, kPane).worldFromCamera.rotate({0.0, 0.0, 1.0}).x == Approx(-1.0));
}

TEST_CASE("orbiting is held short of straight up or down", "[rigplan][orbit]")
{
	rigplan::Orbit orbit;
	rigplan::orbitBy(orbit, 400.0, 500.0);
	CHECK(orbit.pitchDegrees == rigplan::Orbit::kMaxPitchDegrees);
	CHECK(orbit.yawDegrees == Approx(40.0));
	rigplan::orbitBy(orbit, 0.0, -1000.0);
	CHECK(orbit.pitchDegrees == -rigplan::Orbit::kMaxPitchDegrees);
	CHECK(rigplan::viewOf(orbit, kPane).worldFromCamera.rotate({1.0, 0.0, 0.0}).z == Approx(0.0).margin(1e-12));
}

TEST_CASE("a pan keeps what was under the cursor under it", "[rigplan][orbit]")
{
	rigplan::Orbit orbit;
	orbit.yawDegrees = 35;
	orbit.pitchDegrees = 20;
	const math::Vec3d before = orbit.target;
	rigplan::panBy(orbit, {40.0, -25.0}, kPane);
	const math::Vec3d moved = project(rigplan::viewOf(orbit, kPane), before);
	const lain::render::View view = rigplan::viewOf(orbit, kPane);
	CHECK(moved.x == Approx(view.intrinsics.cx + 40.0));
	CHECK(moved.y == Approx(view.intrinsics.cy - 25.0));
}

TEST_CASE("a dolly and a zoom move toward the target, within bounds", "[rigplan][orbit]")
{
	rigplan::Orbit orbit;
	const double start = orbit.distance;
	rigplan::dollyBy(orbit, std::log(2.0));
	CHECK(orbit.distance == Approx(start / 2.0));
	rigplan::zoomBy(orbit, 1.0);
	CHECK(orbit.distance == Approx(start / 2.0 * 0.9));
	rigplan::zoomBy(orbit, -1.0);
	CHECK(orbit.distance == Approx(start / 2.0));
	rigplan::dollyBy(orbit, 100.0);
	CHECK(orbit.distance == rigplan::Orbit::kMinDistance);
	rigplan::dollyBy(orbit, -100.0);
	CHECK(orbit.distance == rigplan::Orbit::kMaxDistance);
}

TEST_CASE("framing a box puts every corner of it in view", "[rigplan][orbit]")
{
	const math::Vec3d lower{-4.0, -3.0, 0.0};
	const math::Vec3d upper{4.0, 3.0, 3.0};
	for (const lain::camera::ImageGeometry pane : {lain::camera::ImageGeometry{960, 540}, lain::camera::ImageGeometry{400, 900}})
	{
		for (double yaw : {0.0, 30.0, 135.0, -100.0})
		{
			rigplan::Orbit orbit;
			orbit.yawDegrees = yaw;
			orbit.pitchDegrees = 30;
			const rigplan::Orbit framed = rigplan::framing(orbit, lower, upper, pane);
			CHECK(framed.yawDegrees == orbit.yawDegrees);
			const lain::render::View view = rigplan::viewOf(framed, pane);
			INFO("pane " << pane.width << " x " << pane.height << " yaw " << yaw);
			for (int i = 0; i < 8; ++i)
			{
				const math::Vec3d corner{i & 1 ? upper.x : lower.x, i & 2 ? upper.y : lower.y, i & 4 ? upper.z : lower.z};
				const math::Vec3d p = project(view, corner);
				CHECK(p.z > view.nearPlane);
				CHECK(p.z < view.farPlane);
				CHECK(p.x > -0.5);
				CHECK(p.x < double(pane.width) - 0.5);
				CHECK(p.y > -0.5);
				CHECK(p.y < double(pane.height) - 0.5);
			}
		}
	}
}
