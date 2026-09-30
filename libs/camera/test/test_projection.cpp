// Projection and unprojection through every model: round trips across the image, the failure
// statuses, the fold domain, containment, and the claim that the kernels are scalar-generic.

#include "testcamera.h"

#include <lain/camera/projection.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace lain::camera;
using namespace lain::camera::testing;

TEST_CASE("every model unprojects and reprojects every pixel to itself", "[camera][projection]")
{
	for (int m = 0; m < kModelCount; ++m)
	{
		const CameraModel model = cameraWith(everyModel(m));
		INFO(displayName(model.distortion()));
		// A grid reaching the image corners, where distortion is strongest.
		for (double u = -0.5; u <= 639.5; u += 639.0 / 8)
		{
			for (double v = -0.5; v <= 479.5; v += 479.0 / 8)
			{
				const Unprojection<double> ray = unproject(model, u, v);
				REQUIRE(ray.ok());
				CHECK(std::sqrt(ray.x * ray.x + ray.y * ray.y + ray.z * ray.z) == Catch::Approx(1.0));
				const Projection<double> back = project(model, ray.x, ray.y, ray.z);
				REQUIRE(back.ok());
				CHECK(back.u == Catch::Approx(u).margin(1e-8));
				CHECK(back.v == Catch::Approx(v).margin(1e-8));
			}
		}
	}
}

TEST_CASE("every model projects and unprojects a point back to its direction", "[camera][projection]")
{
	for (int m = 0; m < kModelCount; ++m)
	{
		const CameraModel model = cameraWith(everyModel(m));
		INFO(displayName(model.distortion()));
		for (const auto& [x, y, z] : {std::tuple{0.0, 0.0, 1.0}, std::tuple{0.3, -0.2, 1.0}, std::tuple{-1.0, 0.7, 2.5}})
		{
			const Projection<double> pixel = project(model, x, y, z);
			REQUIRE(pixel.ok());
			const Unprojection<double> ray = unproject(model, pixel.u, pixel.v);
			REQUIRE(ray.ok());
			const double length = std::sqrt(x * x + y * y + z * z);
			CHECK(ray.x == Catch::Approx(x / length).margin(1e-10));
			CHECK(ray.y == Catch::Approx(y / length).margin(1e-10));
			CHECK(ray.z == Catch::Approx(z / length).margin(1e-10));
		}
	}
}

TEST_CASE("a point that is not in front of the camera is BehindCamera", "[camera][projection]")
{
	for (int m = 0; m < kModelCount; ++m)
	{
		const CameraModel model = cameraWith(everyModel(m));
		INFO(displayName(model.distortion()));
		CHECK(project(model, 0.1, 0.1, 0.0).status == ProjectionStatus::BehindCamera);
		CHECK(project(model, 0.1, 0.1, -1.0).status == ProjectionStatus::BehindCamera);
		CHECK(project(model, 0.1, 0.1, std::numeric_limits<double>::quiet_NaN()).status == ProjectionStatus::BehindCamera);
	}
}

TEST_CASE("a barrel fold bounds the domain where the radial mapping stops increasing", "[camera][projection]")
{
	// With only k1, g(r) = r + k1 r^3 turns over where 1 + 3 k1 r^2 = 0.
	CameraModelParameters p = parametersWith(BrownConrady5{-0.3, 0, 0, 0, 0});
	// The fold's DISTORTED radius is g(1.054) = 0.703; at fx = fy = 800 the image corners reach a
	// distorted radius of 0.50, so the whole image is inside the domain.
	p.intrinsics = {800.0, 800.0, 320.0, 240.0};
	const ModelResult result = CameraModel::create(p);
	REQUIRE(result.model.has_value());
	const CameraModel& model = *result.model;
	const double fold = 1.0540925533894598; // sqrt(1 / 0.9)
	CHECK(model.domain().limit == Catch::Approx(fold).margin(1e-9));
	CHECK(model.domain().limitImage == Catch::Approx(fold * (1 - 0.3 * fold * fold)).margin(1e-9));

	// Past the fold a point is OutsideDomain, however valid its pixel would look...
	CHECK(project(model, 1.2, 0.0, 1.0).status == ProjectionStatus::OutsideDomain);
	CHECK(project(model, 1.0, 0.0, 1.0).ok());
	// ...and a pixel whose distorted radius no ray inside the domain reaches is too.
	const double pastImage = 320.0 + 800.0 * (model.domain().limitImage + 0.05);
	CHECK(unproject(model, pastImage, 240.0).status == ProjectionStatus::OutsideDomain);
}

TEST_CASE("a Kannala-Brandt fold bounds the angle", "[camera][projection]")
{
	// dthetad/dtheta = 1 + 3 k1 theta^2 = 0 at theta = sqrt(1 / 0.9) for k1 = -0.3: about 60 degrees.
	CameraModelParameters p = parametersWith(KannalaBrandt4{-0.3, 0, 0, 0});
	p.intrinsics = {800.0, 800.0, 320.0, 240.0}; // corners at thetad = 0.50, inside the fold's 0.703
	const ModelResult result = CameraModel::create(p);
	REQUIRE(result.model.has_value());
	const CameraModel& model = *result.model;
	CHECK(model.domain().limit == Catch::Approx(1.0540925533894598).margin(1e-9));
	CHECK(project(model, std::tan(1.2), 0.0, 1.0).status == ProjectionStatus::OutsideDomain);
	CHECK(project(model, std::tan(1.0), 0.0, 1.0).ok());
}

TEST_CASE("projection success says nothing about the image rectangle", "[camera][projection]")
{
	const CameraModel model = cameraWith(NoDistortion{});
	const Projection<double> far = project(model, 3.0, 0.0, 1.0); // u = 1820.5
	REQUIRE(far.ok());
	CHECK_FALSE(contains(model, far.u, far.v));
	CHECK(contains(model, -0.5, -0.5));
	CHECK(contains(model, 639.49, 479.49));
	CHECK_FALSE(contains(model, 639.5, 240.0));
	CHECK_FALSE(contains(model, 320.0, -0.51));
}

TEST_CASE("the kernels are scalar-generic: float runs the same code", "[camera][projection]")
{
	// A real automatic-differentiation scalar is slice 2's; float is the witness here that nothing in
	// the kernels is secretly double.
	for (const Distortion& distortion : {Distortion{brownConrady()}, Distortion{kannalaBrandt()}, Distortion{inverseBrownConrady()}})
	{
		const CameraModel model = cameraWith(distortion);
		INFO(displayName(distortion));
		const Projection<double> exact = project(model, 0.3, -0.2, 1.0);
		const Projection<float> single = project(model, 0.3f, -0.2f, 1.0f);
		REQUIRE(single.ok());
		CHECK(single.u == Catch::Approx(exact.u).margin(1e-3));
		CHECK(single.v == Catch::Approx(exact.v).margin(1e-3));
		const Unprojection<float> ray = unproject(model, single.u, single.v);
		REQUIRE(ray.ok());
		CHECK(ray.x / ray.z == Catch::Approx(0.3).margin(1e-4));
	}
}
