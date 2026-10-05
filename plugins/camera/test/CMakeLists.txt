# Aggregator test (Catch2): registerCameraBackends() wires exactly the backends this build has — of
# which there are legitimately none when the plugins are off or unavailable. Asked per plugin, by
# what was BUILT, because a plugin is not one capability: OpenCV renders, detects and estimates,
# and Ceres does none of those.

if(TARGET lain::camera::opencv)
	set(_lain_camera_has_opencv 1)
else()
	set(_lain_camera_has_opencv 0)
endif()

add_executable(test-camera-backends test_backends.cpp)
target_compile_features(test-camera-backends PRIVATE cxx_std_17)
target_compile_definitions(test-camera-backends PRIVATE LAIN_CAMERA_HAS_OPENCV=${_lain_camera_has_opencv})
target_link_libraries(test-camera-backends PRIVATE lain::camera::backends lain::warnings Catch2::Catch2WithMain)

# Windows has no rpath: with the OpenCV plugin built its DLLs must sit beside the executable.
if(TARGET lain::camera::opencv)
	lain_opencv_stage_runtime(test-camera-backends)
endif()

catch_discover_tests(test-camera-backends TEST_PREFIX "(camera::backends) ")

# The real-camera fixture harness (ADR-0016): loading a fixture folder, calibrating its sessions,
# the repeat check between them, and the extended tier's content-hash manifest. Test code, so it
# lives here rather than in libs/camera: nothing ships it. Built in every configuration, since
# loading and the manifest need no backend; the calibrating cases SKIP without one.
add_library(camera-fixture STATIC
	fixture/fixture.cpp
	fixture/manifest.cpp
)
target_compile_features(camera-fixture PUBLIC cxx_std_17)
target_include_directories(camera-fixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(camera-fixture
	PUBLIC lain::camera lain::camera::serialize lain::media lain::data
	PRIVATE lain::io::data lain::io::sequence lain::core lain::log lain::meta lain::string lain::warnings
)

# What a person runs to write or check an extended tier's manifest.
add_executable(camera-fixture-tool fixturetool.cpp)
target_link_libraries(camera-fixture-tool PRIVATE camera-fixture lain::io::data::codecs lain::warnings)

add_executable(test-camera-fixture test_fixture.cpp test_realfixture.cpp)
target_compile_features(test-camera-fixture PRIVATE cxx_std_17)
target_include_directories(test-camera-fixture PRIVATE "${lain_SOURCE_DIR}/libs/camera/test")
# Where committed real-camera fixtures live: one folder each, holding fixture.json.
target_compile_definitions(test-camera-fixture
	PRIVATE LAIN_CAMERA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test-camera-fixture PRIVATE
	camera-fixture
	lain::camera::backends
	lain::io::image
	lain::io::image::codecs
	lain::io::sequence
	lain::io::data::codecs
	lain::testing
	lain::warnings
	Catch2::Catch2WithMain
)
if(TARGET lain::camera::opencv)
	lain_opencv_stage_runtime(test-camera-fixture)
endif()

catch_discover_tests(test-camera-fixture TEST_PREFIX "(camera::fixture) ")
