// The targetless kinds are offered with the feature backends and a refiner, and with no board backend
// at all: they calibrate a camera, and place a rig's cameras, from the scene the footage sees, so a
// build that cannot detect a board can still do both, and offers the model a calibration produces.
//
// Its own executable, staged from empty registries, because registries only grow: test_registration.cpp
// registers every board backend before the feature ones, so it can show that each feature backend is
// needed, but not that the refiner is, nor that nothing of a board is.

#include "nullbackends.h"

#include <lain/camera/flow/register.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera::testing::null;

TEST_CASE("the targetless kinds are offered with feature backends and a refiner, and no board backend",
		  "[camera][flow]")
{
	using Keys = std::vector<std::string>;
	CHECK(camera::availableCameraNodeKeys().empty());

	// Extract, match and pose from what was matched: without a refinement, not yet.
	camera::feature::extractorRegistry().registerType<NullExtractor>("null");
	camera::feature::matcherRegistry().registerType<NullMatcher>("null");
	camera::feature::geometryRegistry().registerType<NullGeometry>("null");
	CHECK(camera::availableCameraNodeKeys().empty());

	// With it, targetless calibration and registration, the fallback's nodes, the model a calibration
	// produces, the rig a registration finds, and nothing that needs a board.
	camera::refinement::refinerRegistry().registerType<NullRefiner>("null");
	CHECK(camera::availableCameraNodeKeys() == Keys{camera::kCalibrateCameraTargetlessKey, camera::kCalibrationMethodKey,
													camera::kNextAttemptKey, camera::kCalibrationAttemptsKey,
													camera::kCameraModelKey, camera::kRegisterCamerasTargetlessKey,
													camera::kRegisteredRigKey});
}
