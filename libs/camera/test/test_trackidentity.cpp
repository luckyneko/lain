// A track's identity is a digest of a versioned canonical text (feature/extraction.cpp), so a change to
// the text changes every identity a registration has reported. Pinned here against a digest computed
// outside lain, with the text spelled out, so such a change has to be deliberate.

#include "syntheticscene.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace scene = lain::camera::testing::scene;

TEST_CASE("a rig track's identity is the digest of its pinned canonical text", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset(2);
	const std::vector<RigFootage> cameras = scene::footage();
	const ExtractionResult result = extractTracks(cameras, scene::groups(cameras));
	REQUIRE(result.ok());

	// Landmark 0's track: where each camera sees it, on the 1/64-pixel grid the text is spelled on.
	const math::Vec2d in0 = *scene::pixelOf(0, 0, 0);
	const math::Vec2d in1 = *scene::pixelOf(1, 0, 0);
	CHECK(std::llround(in0.x * 64.0) == 12712);
	CHECK(std::llround(in0.y * 64.0) == 10125);
	CHECK(std::llround(in1.x * 64.0) == 13591);
	CHECK(std::llround(in1.y * 64.0) == 10770);
	const Track* track = nullptr;
	for (const Track& t : result.trackSet.tracks)
	{
		if (math::length(t.observations.front().pixel - in0) < 1e-9)
			track = &t;
	}
	REQUIRE(track != nullptr);
	REQUIRE(track->observations.size() == 2);

	// The text, a version line and then each observation's camera (length-prefixed) and pixel cell:
	//   "lain feature track 1\n5:cam00 12712 10125\n5:cam01 13591 10770\n"
	// and its SHA-256, computed by sha256sum and by Python's hashlib, which agree.
	CHECK(track->identity == "46ccded53edd86a738ffe4e8df5454596b07a225f2b0f3ec03b7002f06f47764");
}
