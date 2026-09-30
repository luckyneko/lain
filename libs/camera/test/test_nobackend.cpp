// The facades with NO backend registered. Its own executable, because a core::Factory only grows: in
// test-camera, a stand-in registered by another case would already be there.

#include "testboard.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain;
using namespace lain::camera::board;
using namespace lain::camera::testing;

TEST_CASE("with no backend, detect reports the missing capability as a failed report", "[camera][board]")
{
	REQUIRE_FALSE(canDetect());
	const DetectionReport report = detect(image::Image{64, 48, image::PixelFormat::Gray8}, {}, specification());
	CHECK(report.status == DetectionStatus::Failed);
	CHECK_FALSE(report.observation.has_value());
	REQUIRE(report.rejections.size() == 1);
	CHECK(report.rejections[0].reason == Rejection::NoBackend);
	CHECK(report.stats.cornersExpected == 24);
}

TEST_CASE("with no backend, render has nothing to give", "[camera][board]")
{
	REQUIRE_FALSE(canRender());
	CHECK_FALSE(render(pattern()).has_value());
}
