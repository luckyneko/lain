// registerCameraBackends() registers exactly what the build enabled: in the default configuration,
// nothing, and the capability queries say so.

#include <lain/camera/backends.h>
#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain::camera;

TEST_CASE("the aggregator registers the build's backends, and only those", "[camera][backends]")
{
	REQUIRE_FALSE(board::canDetect());
	registerCameraBackends();
	const bool expected = LAIN_CAMERA_BACKEND_COUNT > 0;
	CHECK(board::canDetect() == expected);
	CHECK(board::canRender() == expected);
	CHECK(board::detectorRegistry().keys().size() == std::size_t(LAIN_CAMERA_BACKEND_COUNT));
}
