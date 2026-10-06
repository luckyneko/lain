// Registration in a build that can pose a board but cannot refine a registration: a pose solver is
// registered and no refiner is. Its own executable, since a core::Factory only grows.

#include "syntheticrig.h"

#include <lain/camera/registration/board.h>

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
