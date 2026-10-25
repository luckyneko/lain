// A synthetic capture's ground truth (truth.h; CONTEXT.md, "Ground truth"): poses, the board corners in
// view of each camera through its REALISED model, and the rig relative to a reference camera.

#include "testplan.h"

#include <lain/camera/projection.h>
#include <lain/rigplan/mover.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/truth.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using lain::math::RigidTransformd;
using lain::math::Vec3d;

static lain::rigplan::RigPlan planWithBoard()
{
	lain::rigplan::RigPlan plan = testplan::plan();
	plan.boards.push_back(testplan::board());
	return plan;
}

static lain::rigplan::Realisation realised(const lain::rigplan::RigPlan& plan)
{
	lain::rigplan::RealisationResult result = lain::rigplan::realise(plan);
	REQUIRE(result.realisation.has_value());
	return std::move(*result.realisation);
}

static double angleBetween(const lain::math::Quatd& a, const lain::math::Quatd& b)
{
	return 2.0 * std::acos(std::min(1.0, std::abs(lain::math::dot(a, b))));
}

TEST_CASE("the truth's poses are the realisation's", "[rigplan][truth]")
{
	const lain::rigplan::RigPlan plan = testplan::plan();
	const lain::rigplan::Realisation realisation = realised(plan);
	const auto poses = lain::rigplan::cameraPoses(realisation);
	REQUIRE(poses.size() == 3);
	for (std::size_t i = 0; i < poses.size(); ++i)
	{
		REQUIRE(poses[i].id == realisation.cameras[i].id);
		REQUIRE(poses[i].name == plan.cameras[i].name);
		REQUIRE(poses[i].planFromCamera.rotation() == realisation.cameras[i].planFromCamera.rotation());
		REQUIRE(poses[i].planFromCamera.translation() == realisation.cameras[i].planFromCamera.translation());
	}
}

TEST_CASE("a board's corners in view are where the realised model projects them", "[rigplan][truth]")
{
	const lain::rigplan::RigPlan plan = planWithBoard();
	const lain::rigplan::Realisation realisation = realised(plan);
	const lain::camera::board::Specification specification = *lain::rigplan::specificationOf(plan.boards[0]);
	const std::uint32_t corners = specification.pattern().cornerCount(); // 6 x 4

	// At frame 0 the board is face on to cam0, 0.875 m straight ahead: every corner is in view.
	const auto truth = lain::rigplan::boardTruth(plan, realisation, 0);
	REQUIRE(truth.size() == 1);
	REQUIRE(truth[0].board == testplan::boardId);
	REQUIRE(truth[0].name == "charuco");
	REQUIRE(truth[0].frame == 0);
	REQUIRE(truth[0].cameras.size() == 3);
	const lain::rigplan::CameraCorners& cam0 = truth[0].cameras[0];
	REQUIRE(cam0.name == "cam0");
	REQUIRE(cam0.inView.size() == corners);

	const lain::rigplan::RealisedCamera& camera = realisation.cameras[0];
	const RigidTransformd cameraFromBoard = camera.planFromCamera.inverse() * truth[0].planFromBoard;
	const auto nominal = lain::rigplan::nominalModel(plan.specifications.sensors[0], plan.specifications.lenses[0]);
	REQUIRE(nominal.model.has_value());
	double nominalOffset = 0;
	for (const lain::rigplan::CornerTruth& c : cam0.inView)
	{
		const Vec3d p = cameraFromBoard.apply(*specification.cornerPosition(c.corner));
		const auto projected = lain::camera::project(camera.model, p.x, p.y, p.z);
		REQUIRE(projected.ok());
		REQUIRE(c.pixel.x == projected.u);
		REQUIRE(c.pixel.y == projected.v);
		const auto throughNominal = lain::camera::project(*nominal.model, p.x, p.y, p.z);
		nominalOffset = std::max(nominalOffset, std::hypot(c.pixel.x - throughNominal.u, c.pixel.y - throughNominal.v));
	}
	// The test plan is realised at full intrinsic scale, so the realised model and the datasheet's part
	// by pixels: the truth is the realised one.
	REQUIRE(nominalOffset > 1.0);

	// cam1 looks down past it, and cam2 is across the room facing elsewhere: none in view.
	REQUIRE(truth[0].cameras[1].inView.empty());

	// By frame 12 it has moved right until it leaves cam0's image: some corners in view, each on it.
	const auto later = lain::rigplan::boardTruth(plan, realisation, 12);
	const lain::rigplan::CameraCorners& partial = later[0].cameras[0];
	REQUIRE(!partial.inView.empty());
	REQUIRE(partial.inView.size() < corners);
	for (const lain::rigplan::CornerTruth& c : partial.inView)
	{
		REQUIRE(c.pixel.x >= -0.5);
		REQUIRE(c.pixel.x < 1280 - 0.5);
		REQUIRE(c.pixel.y >= -0.5);
		REQUIRE(c.pixel.y < 720 - 0.5);
	}
	for (std::size_t i = 1; i < partial.inView.size(); ++i)
		REQUIRE(partial.inView[i].corner > partial.inView[i - 1].corner);
}

TEST_CASE("a camera behind a board sees none of its corners", "[rigplan][truth]")
{
	// The board turned round: cam0 is behind its print, where the board itself hides every corner.
	lain::rigplan::RigPlan plan = planWithBoard();
	plan.boards[0].path.keys = {{0, {2.0, 4.0, 1.5}, {180.0, 0.0, 0.0}}};
	const auto truth = lain::rigplan::boardTruth(plan, realised(plan), 0);
	REQUIRE(truth[0].cameras[0].inView.empty());
}

TEST_CASE("the truth's rig is relative to the reference camera, metric, named by camera", "[rigplan][truth]")
{
	lain::rigplan::RigPlan plan = testplan::plan();
	const lain::rigplan::Realisation realisation = realised(plan);

	// No reference named: the first camera.
	REQUIRE(lain::rigplan::referenceCamera(plan, realisation) == &realisation.cameras[0]);
	const lain::camera::registration::RegisteredRig rig = lain::rigplan::truthRig(plan, realisation);
	REQUIRE(rig.reference.value == "cam0");
	REQUIRE(rig.scale.scale == lain::camera::registration::Scale::Metric);
	REQUIRE(rig.cameras.size() == 3);
	for (std::size_t i = 1; i < rig.cameras.size(); ++i)
		REQUIRE(rig.cameras[i - 1].camera < rig.cameras[i].camera);
	const RigidTransformd reference = *rig.referenceFromCamera({"cam0"});
	REQUIRE(reference.rotation() == lain::math::Quatd{1.0, 0.0, 0.0, 0.0});
	REQUIRE(reference.translation() == Vec3d{0.0});
	for (const lain::rigplan::RealisedCamera& camera : realisation.cameras)
	{
		const RigidTransformd expected = realisation.cameras[0].planFromCamera.inverse() * camera.planFromCamera;
		const RigidTransformd got = *rig.referenceFromCamera({camera.name});
		REQUIRE(angleBetween(got.rotation(), expected.rotation()) < 1e-12);
		REQUIRE(lain::math::length(got.translation() - expected.translation()) < 1e-12);
	}

	// Named: cam2. Its rig agrees with the first, rebased: what a registration choosing cam2 is scored
	// against.
	plan.reference = testplan::cam2Id;
	REQUIRE(lain::rigplan::referenceCamera(plan, realisation) == &realisation.cameras[2]);
	const lain::camera::registration::RegisteredRig rebased = lain::rigplan::truthRig(plan, realisation);
	REQUIRE(rebased.reference.value == "cam2");
	const RigidTransformd cam0FromCam2 = *rig.referenceFromCamera({"cam2"});
	for (const char* name : {"cam0", "cam1"})
	{
		const RigidTransformd expected = cam0FromCam2.inverse() * *rig.referenceFromCamera({name});
		const RigidTransformd got = *rebased.referenceFromCamera({name});
		REQUIRE(angleBetween(got.rotation(), expected.rotation()) < 1e-12);
		REQUIRE(lain::math::length(got.translation() - expected.translation()) < 1e-12);
	}
}

TEST_CASE("a reference that names no camera is refused", "[rigplan][truth]")
{
	lain::rigplan::RigPlan plan = testplan::plan();
	plan.reference = testplan::lightId; // an owner, but not a camera
	const auto problems = lain::rigplan::check(plan);
	REQUIRE(problems.size() == 1);
	REQUIRE(problems[0].problem == lain::rigplan::PlanProblem::UnknownReference);
}
