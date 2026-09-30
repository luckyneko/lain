# lain::camera tests (Catch2). Pure unit tests over the production kernels: no driver, no files, no
# third-party code.

add_executable(test-camera
	test_distortion.cpp
	test_projection.cpp
	test_cameramodel.cpp
)

target_compile_features(test-camera PRIVATE cxx_std_17)
target_link_libraries(test-camera PRIVATE lain::camera lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-camera TEST_PREFIX "(camera) ")
