// Targetless registration end to end through both real backends: three fixed cameras over a rendered
// textured scene with a disc crossing it, SIFT features matched and verified by the OpenCV plugin,
// the rig refined by the Ceres plugin. The scene is the OpenCV feature producer's footage case
// (test_features.cpp) exactly, so its tracks read beside that case's.

#include "registration.h"
#include "renderedrig.h"
#include "rigcompare.h"
#include "testcamera.h"

#include <lain/camera/registration/targetless.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;

TEST_CASE("three cameras over a rendered scene register without a target", "[camera][registration][targetless]")
{
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	synthetic::Scene scene = synthetic::defaultScene();
	scene.noise = 1.0;
	scene.occluder = synthetic::Occluder{{-1.6, 0.0, -1.0}, {0.4, 0.0, 0.0}, 0.35, {77, 0.03}};
	const std::vector<double> angles{-0.25, 0.0, 0.25};
	std::vector<math::RigidTransformd> poses;
	for (const double a : angles)
		poses.push_back(synthetic::arcCamera(a));
	const rendered::Rig rig =
		rendered::rig(scene, camera::testing::cameraWith(camera::testing::brownConrady()), poses, 9);

	registration::Request request;
	request.reference = rig.identities[1];
	request.resamples = 4;
	const registration::Report report =
		registration::targetless::registerCameras(rig.footage, rig.groups, feature::ExtractionRequest{}, request);
	INFO(report.toString());
	REQUIRE(report.status == registration::RegistrationStatus::Succeeded);
	const registration::TargetlessRecord& record = std::get<registration::TargetlessRecord>(report.reproducibility.method);
	CHECK(record.extractor.backend == "opencv");
	CHECK(record.matcher.backend == "opencv");
	CHECK(record.geometry.backend == "opencv");
	CHECK(report.reproducibility.refiner.backend == "ceres");
	const registration::TargetlessDiagnostics& t = std::get<registration::TargetlessDiagnostics>(report.diagnostics.method);
	REQUIRE(t.trackSet.has_value());

	const camera::testing::RigDifference truth =
		camera::testing::compare(camera::testing::rebased(poses, 1), camera::testing::posesOf(report, rig.identities));
	const auto* held = std::get_if<registration::HeldOutEvidence>(&report.heldOut);
	REQUIRE(held != nullptr);
	// Measured: 429 tracks, as the footage case extracts, 86 of them held out; the worst camera
	// 0.37 mrad and 1.6 mm from the truth (the cameras stand 3.2 m from the scene and 0.8 m apart),
	// a held-out transfer of 1.06 mrad (about half a pixel at this focal length of 500), 6 outliers,
	// Ready. 13.6 s in Debug, nearly all of it drawing the 15 sampled frames.
	CHECK(truth.worstRotation < 0.001);
	CHECK(truth.worstCentre < 0.004);
	CHECK(held->rmsAngle < 0.002);
	CHECK(report.verdict == Verdict::Ready);
	// Every track is a point of the scene behind the disc: what crossed it is not in the rig.
	// Measured: none off it, and every member within 6.7 px of where the still scene puts it. With
	// extraction's static rule disabled, 22 tracks of the disc got in and the registration was still
	// Ready, since a synchronised rig sees a moving feature at one instant from every camera; only the
	// still scene behind it can tell. The bound leaves room for the wrong match a run may verify (1%).
	const rendered::StillScene still = rendered::stillScene(rig, *t.trackSet);
	CHECK(still.off * 100 <= t.trackSet->tracks.size());
}
