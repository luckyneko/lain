# lain::camera tests (Catch2). Pure unit tests over the production code: no driver, no files, no
# third-party code. A build with the OpenCV plugin tests that backend in plugins/camera/opencv.

add_executable(test-camera
	test_distortion.cpp
	test_projection.cpp
	test_cameramodel.cpp
	test_boardpattern.cpp
	test_boardseams.cpp
)
target_compile_features(test-camera PRIVATE cxx_std_17)
target_link_libraries(test-camera PRIVATE lain::camera lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera TEST_PREFIX "(camera) ")

# The facades with an EMPTY registry. A core::Factory only grows, so this cannot share an executable
# with a case that registers a stand-in backend.
add_executable(test-camera-nobackend test_nobackend.cpp)
target_compile_features(test-camera-nobackend PRIVATE cxx_std_17)
target_link_libraries(test-camera-nobackend PRIVATE lain::camera lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-nobackend TEST_PREFIX "(camera) ")
