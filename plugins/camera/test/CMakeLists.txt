# Aggregator test (Catch2): registerCameraBackends() wires exactly the build-enabled backends — of
# which there are legitimately zero in the default configuration.

list(LENGTH _lain_camera_backends _lain_camera_backend_count)

add_executable(test-camera-backends test_backends.cpp)
target_compile_features(test-camera-backends PRIVATE cxx_std_17)
target_compile_definitions(test-camera-backends PRIVATE LAIN_CAMERA_BACKEND_COUNT=${_lain_camera_backend_count})
target_link_libraries(test-camera-backends PRIVATE lain::camera::backends lain::warnings Catch2::Catch2WithMain)

# Windows has no rpath: with the OpenCV plugin enabled its DLLs must sit beside the executable.
if(LAIN_CAMERA_OPENCV)
	lain_opencv_stage_runtime(test-camera-backends)
endif()

catch_discover_tests(test-camera-backends TEST_PREFIX "(camera::backends) ")
