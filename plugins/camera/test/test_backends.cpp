// registerCameraBackends() registers exactly what the build has: without the OpenCV plugin (off, or
// no prebuilt for the platform), no renderer, detector or estimator, and the capability queries say
// so. The Ceres plugin registers none of those, so whether it was built changes nothing here.

#include <lain/camera/backends.h>
#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain::camera;

TEST_CASE("the aggregator registers the build's backends, and only those", "[camera][backends]")
{
	REQUIRE_FALSE(board::canDetect());
	registerCameraBackends();
	const bool opencv = LAIN_CAMERA_HAS_OPENCV == 1;
	CHECK(board::canDetect() == opencv);
	CHECK(board::canRender() == opencv);
	CHECK(board::detectorRegistry().keys().size() == std::size_t(opencv ? 1 : 0));
}
