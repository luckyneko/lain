// Model comparison and rig comparison (lain/camera/comparison.h), each against what the geometry says
// the answer is: a focal scale about the principal point moves a pixel by the scale's excess times its
// distance from it, and a rig's differences are the turns and offsets put into it.

#include "testcamera.h"

#include <lain/camera/comparison.h>
#include <lain/math/rigidtransform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera;

static CameraModel modelOf(const ImageGeometry& image, const Intrinsics& intrinsics, const Distortion& distortion)
{
	CameraModelParameters p;
	p.image = image;
	p.intrinsics = intrinsics;
	p.distortion = distortion;
	ModelResult result = CameraModel::create(p);
	REQUIRE(result.model.has_value());
	return *result.model;
}

TEST_CASE("a model compared with itself is zero at every sample", "[camera][comparison]")
{
	const CameraModel model = testing::cameraWith(testing::brownConrady());
	const ModelComparison c = compareModels(model, model);
	REQUIRE(c.compared());
	// 640 wide: 0, 16, ..., 624 and the last column, 639. 480 high: 0, 16, ..., 464 and 479.
	CHECK(c.samples == 41 * 31);
	CHECK(c.unreached == 0);
	CHECK(c.focalDifference == 0);
	// Unprojection iterates for a forward Brown-Conrady model, so a round trip is not exact.
	CHECK(c.worstPixels < 1e-8);
	CHECK(c.meanPixels <= c.worstPixels);
}

TEST_CASE("a focal scale moves each ray by its distance from the principal point", "[camera][comparison]")
{
	// The principal point at the top-left pixel, so the farthest pixel is the bottom-right one: the
	// corner a grid stopping 15 px short of the edges would miss.
	const ImageGeometry image{640, 480};
	const CameraModel expected = modelOf(image, {500.0, 500.0, 0.5, 0.5}, NoDistortion{});
	const CameraModel found = modelOf(image, {505.0, 505.0, 0.5, 0.5}, NoDistortion{});
	const ModelComparison c = compareModels(expected, found);
	REQUIRE(c.compared());
	CHECK(c.focalDifference == Catch::Approx(0.01).epsilon(1e-12));
	CHECK(c.worstAt.x == 639.0);
	CHECK(c.worstAt.y == 479.0);
	// The expected model's ray at (u, v), projected through the found one, lands 1.01 times as far from
	// the principal point: the expected rays, through the found model, in that direction.
	CHECK(c.worstPixels == Catch::Approx(0.01 * std::hypot(639.0 - 0.5, 479.0 - 0.5)).epsilon(1e-9));

	double sum = 0;
	std::size_t count = 0;
	for (const double v : {0.0, 16.0, 32.0, 48.0, 64.0, 80.0, 96.0, 112.0, 128.0, 144.0, 160.0, 176.0, 192.0, 208.0,
						   224.0, 240.0, 256.0, 272.0, 288.0, 304.0, 320.0, 336.0, 352.0, 368.0, 384.0, 400.0, 416.0,
						   432.0, 448.0, 464.0, 479.0})
	{
		for (double u = 0; u < 640; u += 16)
		{
			sum += 0.01 * std::hypot(u - 0.5, v - 0.5);
			++count;
		}
		sum += 0.01 * std::hypot(639.0 - 0.5, v - 0.5);
		++count;
	}
	CHECK(c.samples == count);
	CHECK(c.meanPixels == Catch::Approx(sum / double(count)).epsilon(1e-9));
}

TEST_CASE("a ray the found model cannot reach makes the comparison infinite", "[camera][comparison]")
{
	// The expected camera sees wide; the found one sees narrowly through a lens that folds about 46
	// degrees off its axis, well inside the expected camera's corners, so its own corners are fine and
	// the expected camera's outer rays are beyond its domain.
	const ImageGeometry image{640, 480};
	const CameraModel expected = modelOf(image, {200.0, 200.0, 319.5, 239.5}, NoDistortion{});
	const CameraModel found = modelOf(image, {2000.0, 2000.0, 319.5, 239.5}, BrownConrady5{-0.3, 0, 0, 0, 0});
	const ModelComparison c = compareModels(expected, found);
	REQUIRE(c.compared());
	CHECK(c.unreached > 0);
	CHECK(c.unreached < c.samples);
	CHECK(std::isinf(c.worstPixels));
	CHECK(std::isinf(c.meanPixels));
	CHECK(c.toString().find("cannot reach") != std::string::npos);
}

TEST_CASE("models of different images are not compared", "[camera][comparison]")
{
	const CameraModel a = testing::cameraWith(NoDistortion{});
	const CameraModel b = modelOf({320, 240}, {250.0, 255.0, 160.5, 119.5}, NoDistortion{});
	const ModelComparison c = compareModels(a, b);
	CHECK_FALSE(c.compared());
	CHECK(c.status == ModelComparisonStatus::DifferentImages);
	CHECK(c.detail.find("640x480") != std::string::npos);
	CHECK(c.detail.find("320x240") != std::string::npos);
	CHECK(c.toString().rfind("not compared", 0) == 0);
}

// A rig of cameras named `names`, at `worldFromCamera`, relative to `reference` (one of them), the
// reference the identity exactly, as a registration reports one.
static registration::RegisteredRig rigOf(const std::vector<std::string>& names,
										 const std::vector<math::RigidTransformd>& worldFromCamera,
										 const std::string& reference, registration::Scale scale)
{
	std::size_t r = 0;
	while (names[r] != reference)
		++r;
	const math::RigidTransformd referenceFromWorld = worldFromCamera[r].inverse();
	registration::RegisteredRig rig;
	rig.reference = capture::CameraIdentity{reference};
	rig.scale = {scale, "the test's"};
	for (std::size_t c = 0; c < names.size(); ++c)
	{
		rig.cameras.push_back({capture::CameraIdentity{names[c]},
							   c == r ? math::RigidTransformd{} : referenceFromWorld * worldFromCamera[c]});
	}
	return rig;
}

static math::RigidTransformd pose(double yaw, double pitch, const math::Vec3d& centre)
{
	return {math::angleAxis(yaw, math::Vec3d{0, 1, 0}) * math::angleAxis(pitch, math::Vec3d{1, 0, 0}), centre};
}

static const std::vector<std::string> kNames{"a", "b", "c"};
static const std::vector<math::RigidTransformd> kWorld{pose(0.1, 0.02, {0, 0, 0}), pose(-0.2, 0.05, {0.6, 0.1, 0}),
													   pose(0.35, -0.04, {1.1, -0.2, 0.3})};

TEST_CASE("a rig compared with itself is zero, at scale 1", "[camera][comparison]")
{
	const registration::RegisteredRig rig = rigOf(kNames, kWorld, "a", registration::Scale::Metric);
	const RigComparison c = compareRigs(rig, rig);
	REQUIRE(c.compared());
	CHECK_FALSE(c.derivedAgain);
	CHECK(c.reference.value == "a");
	CHECK(c.scale == 1.0);
	REQUIRE(c.cameras.size() == 3);
	// conj(q) q of a quaternion one ulp off unit length is not the identity exactly, so a rotation
	// against itself reads ~1e-17 rather than 0; a centre against itself is exact.
	for (const CameraDifference& d : c.cameras)
	{
		CHECK(d.rotation < 1e-15);
		CHECK(d.centre == 0);
	}
	CHECK(c.onlyExpected.empty());
	CHECK(c.onlyFound.empty());
}

TEST_CASE("a camera turned and moved shows in that camera alone", "[camera][comparison]")
{
	const registration::RegisteredRig expected = rigOf(kNames, kWorld, "a", registration::Scale::Metric);
	registration::RegisteredRig found = expected;
	// Camera c turned 2 mrad about its own forward axis, its centre unmoved: the scale fit sees nothing.
	found.cameras[2].referenceFromCamera =
		found.cameras[2].referenceFromCamera * math::RigidTransformd{math::angleAxis(0.002, math::Vec3d{0, 0, 1}), {}};
	const RigComparison c = compareRigs(expected, found);
	REQUIRE(c.compared());
	CHECK(c.scale == Catch::Approx(1.0).epsilon(1e-12));
	CHECK(c.cameras[0].rotation == 0);
	CHECK(c.cameras[1].rotation < 1e-12);
	CHECK(c.cameras[2].rotation == Catch::Approx(0.002).epsilon(1e-9));
	CHECK(c.worstRotation()->camera.value == "c");
	CHECK(c.worstCentre()->centre < 1e-12);
}

TEST_CASE("a rig of another scale is scaled onto the expected one", "[camera][comparison]")
{
	// A targetless registration's centres share one arbitrary scale: here a quarter of the truth's.
	const registration::RegisteredRig expected = rigOf(kNames, kWorld, "b", registration::Scale::Metric);
	registration::RegisteredRig found = expected;
	found.scale = {registration::Scale::Arbitrary, "the test's"};
	for (registration::RegisteredCamera& camera : found.cameras)
	{
		camera.referenceFromCamera =
			math::RigidTransformd{camera.referenceFromCamera.rotation(), camera.referenceFromCamera.translation() * 0.25};
	}
	const RigComparison c = compareRigs(expected, found);
	REQUIRE(c.compared());
	CHECK(c.scale == Catch::Approx(4.0).epsilon(1e-12));
	CHECK(c.foundScale == registration::Scale::Arbitrary);
	CHECK(c.worstCentre()->centre < 1e-12);
	CHECK(c.worstRotation()->rotation < 1e-15);
}

TEST_CASE("an expected rig of another reference is derived again, never the found one rebased", "[camera][comparison]")
{
	// The truth relative to a; the registration chose b, and got camera a turned 1 mrad. Derived again
	// for b, the truth puts the error on a alone. Rebasing the result onto a instead would spread a's
	// error over every other camera, since a's own transform is then what every other one is taken from.
	const registration::RegisteredRig expected = rigOf(kNames, kWorld, "a", registration::Scale::Metric);
	registration::RegisteredRig found = rigOf(kNames, kWorld, "b", registration::Scale::Metric);
	found.cameras[0].referenceFromCamera =
		found.cameras[0].referenceFromCamera * math::RigidTransformd{math::angleAxis(0.001, math::Vec3d{1, 0, 0}), {}};

	const RigComparison c = compareRigs(expected, found);
	REQUIRE(c.compared());
	CHECK(c.derivedAgain);
	CHECK(c.reference.value == "b");
	REQUIRE(c.cameras.size() == 3);
	CHECK(c.cameras[0].rotation == Catch::Approx(0.001).epsilon(1e-9));
	CHECK(c.cameras[1].rotation == 0); // the reference: the identity exactly on both sides
	CHECK(c.cameras[2].rotation < 1e-12);
	CHECK(c.worstCentre()->centre < 1e-12);
	CHECK(c.toString().find("derived again") != std::string::npos);

	// And derived again is the truth built for b directly, to rounding.
	const RigComparison direct = compareRigs(rigOf(kNames, kWorld, "b", registration::Scale::Metric), found);
	REQUIRE(direct.compared());
	CHECK_FALSE(direct.derivedAgain);
	for (std::size_t i = 0; i < 3; ++i)
	{
		CHECK(std::abs(direct.cameras[i].rotation - c.cameras[i].rotation) < 1e-12);
		CHECK(std::abs(direct.cameras[i].centre - c.cameras[i].centre) < 1e-12);
	}
}

TEST_CASE("cameras only one rig holds are named, and the rest compared", "[camera][comparison]")
{
	const registration::RegisteredRig expected = rigOf(kNames, kWorld, "a", registration::Scale::Metric);
	const registration::RegisteredRig found =
		rigOf({"a", "b", "d"}, {kWorld[0], kWorld[1], pose(0.0, 0.0, {2, 0, 0})}, "a", registration::Scale::Metric);
	const RigComparison c = compareRigs(expected, found);
	REQUIRE(c.compared());
	REQUIRE(c.cameras.size() == 2);
	CHECK(c.cameras[0].camera.value == "a");
	CHECK(c.cameras[1].camera.value == "b");
	REQUIRE(c.onlyExpected.size() == 1);
	CHECK(c.onlyExpected[0].value == "c");
	REQUIRE(c.onlyFound.size() == 1);
	CHECK(c.onlyFound[0].value == "d");
	CHECK(c.toString().find("only expected: c") != std::string::npos);
	CHECK(c.toString().find("only found: d") != std::string::npos);
}

TEST_CASE("a found rig relative to a camera the expected one lacks is not compared", "[camera][comparison]")
{
	const registration::RegisteredRig expected = rigOf(kNames, kWorld, "a", registration::Scale::Metric);
	const registration::RegisteredRig found =
		rigOf({"b", "d"}, {kWorld[1], pose(0.0, 0.0, {2, 0, 0})}, "d", registration::Scale::Metric);
	const RigComparison c = compareRigs(expected, found);
	CHECK_FALSE(c.compared());
	CHECK(c.status == RigComparisonStatus::UnknownReference);
	CHECK(c.detail.find("relative to d") != std::string::npos);
	CHECK(c.worstRotation() == nullptr);
	CHECK(c.toString().rfind("not compared", 0) == 0);
}

TEST_CASE("a rig comparison's line names its worst cameras", "[camera][comparison]")
{
	const registration::RegisteredRig expected = rigOf(kNames, kWorld, "a", registration::Scale::Metric);
	registration::RegisteredRig found = expected;
	// b turned 3 mrad, c 5 cm out sideways: the worst rotation is b's and the worst centre c's.
	found.cameras[1].referenceFromCamera =
		found.cameras[1].referenceFromCamera * math::RigidTransformd{math::angleAxis(0.003, math::Vec3d{0, 1, 0}), {}};
	found.cameras[2].referenceFromCamera = math::RigidTransformd{
		found.cameras[2].referenceFromCamera.rotation(), found.cameras[2].referenceFromCamera.translation() + math::Vec3d{0, 0.05, 0}};
	const RigComparison c = compareRigs(expected, found);
	REQUIRE(c.compared());
	CHECK(c.worstRotation()->camera.value == "b");
	CHECK(c.worstCentre()->camera.value == "c");
	const std::string line = c.toString();
	INFO(line);
	CHECK(line.find("3 cameras relative to a") == 0);
	CHECK(line.find("mrad (b)") != std::string::npos);
	CHECK(line.find("mm (c)") != std::string::npos);
}
