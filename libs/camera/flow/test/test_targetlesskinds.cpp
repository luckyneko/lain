// Targetless registration is offered with the feature backends and a refiner, and with no board
// backend at all: it places cameras from the scene they share, so a build that cannot detect a board
// can still register a rig.
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

TEST_CASE("targetless registration is offered with feature backends and a refiner, and no board backend",
		  "[camera][flow]")
{
	using Keys = std::vector<std::string>;
	CHECK(camera::availableCameraNodeKeys().empty());

	// Extract, match and pose from what was matched: without the refinement of the rig, not yet.
	camera::feature::extractorRegistry().registerType<NullExtractor>("null");
	camera::feature::matcherRegistry().registerType<NullMatcher>("null");
	camera::feature::geometryRegistry().registerType<NullGeometry>("null");
	CHECK(camera::availableCameraNodeKeys().empty());

	// With it, targetless registration, and nothing that needs a board.
	camera::registration::refinerRegistry().registerType<NullRefiner>("null");
	CHECK(camera::availableCameraNodeKeys() == Keys{camera::kRegisterCamerasTargetlessKey});
}
