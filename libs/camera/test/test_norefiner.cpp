// Registration in a build that can pose but cannot refine: a board pose solver, a feature extractor
// and a geometry solver are registered, and no refiner and no feature matcher are. Its own executable,
// since a core::Factory only grows.

#include "syntheticrig.h"
#include "syntheticscene.h"

#include <lain/camera/registration/board.h>
#include <lain/camera/registration/targetless.h>

#include <catch2/catch_test_macros.hpp>

#include <variant>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::testing;

TEST_CASE("with a pose solver and no refiner, a registration fails for want of the refiner", "[camera][registration]")
{
	// The rig's scene without its stand-ins: only the pose solver is registered here.
	board::poseSolverRegistry().registerType<rig::TruthPoseSolver>("truth");
	REQUIRE_FALSE(registration::canRefine());
	rig::Scene& s = rig::scene();
	s = rig::Scene{};
	s.referenceFromCamera.assign(2, math::RigidTransformd{});
	s.referenceFromBoard.assign(4, math::RigidTransformd{math::Quatd{1, 0, 0, 0}, math::Vec3d{0, 0, 1}});
	s.sees.assign(2, std::vector<bool>(4, true));
	const registration::Report report =
		registration::board::registerCameras(rig::cameras(), rig::groups(), specification(), registration::Request{});
	REQUIRE(report.status == registration::RegistrationStatus::Failed);
	CHECK(report.failures.front().failure == registration::Failure::NoRefiner);
	CHECK(report.failures.front().detail.find("-DLAIN_CAMERA_CERES=ON") != std::string::npos);
	CHECK(std::get<registration::BoardDiagnostics>(report.diagnostics.method).observations == 0); // refused before a single pose was solved
}

TEST_CASE("with an extractor and a geometry solver but no matcher or refiner, a targetless registration says which",
		  "[camera][registration]")
{
	namespace scene = testing::scene;
	feature::extractorRegistry().registerType<scene::TruthExtractor>("truth");
	feature::geometryRegistry().registerType<scene::TruthGeometry>("truth");
	REQUIRE_FALSE(feature::canMatch());
	REQUIRE_FALSE(registration::canRefine());
	scene::reset(3, 40);
	std::vector<registration::RigCamera> cameras;
	for (std::size_t c = 0; c < 3; ++c)
		cameras.push_back({capture::CameraIdentity{scene::identityOf(c)}, scene::model()});

	// Its tracks need posing and refining, and this build can pose.
	const registration::Report fromTracks =
		registration::targetless::registerCameras(cameras, scene::trackSetOf(), registration::Request{});
	REQUIRE(fromTracks.status == registration::RegistrationStatus::Failed);
	CHECK(fromTracks.failures.front().failure == registration::Failure::NoRefiner);

	// Its footage needs matching first, and this build cannot match.
	const std::vector<RigFootage> footage = scene::footage();
	const registration::Report fromFootage =
		registration::targetless::registerCameras(footage, scene::groups(footage), {}, registration::Request{});
	REQUIRE(fromFootage.status == registration::RegistrationStatus::Failed);
	CHECK(fromFootage.failures.front().failure == registration::Failure::NoFeatureMatcher);
	CHECK(scene::scene().decodes.empty()); // refused before a frame was decoded
}
