# lain::camera tests (Catch2). Pure unit tests over the production code: no driver, no files, no
# third-party code. A build with the OpenCV plugin tests that backend in plugins/camera/opencv.

add_executable(test-camera
	test_distortion.cpp
	test_projection.cpp
	test_cameramodel.cpp
	test_boardpattern.cpp
	test_boardseams.cpp
	test_capturegroup.cpp
	test_registrationcontracts.cpp
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

# The calibration method module over stand-in backends that answer from a known truth. Its own
# executable, so those stand-ins are the only backends registered.
add_executable(test-camera-calibration test_calibration.cpp)
target_compile_features(test-camera-calibration PRIVATE cxx_std_17)
target_link_libraries(test-camera-calibration PRIVATE lain::camera lain::testing lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-calibration TEST_PREFIX "(camera) ")

# The board registration method module over stand-in backends answering from a known rig. Its own
# executable, so those stand-ins are the only backends registered. The golden case compares a whole
# report with golden/boardregistration.txt, and writes what it produced beside the build when the
# two differ.
add_executable(test-camera-registration test_registration.cpp test_registrationgolden.cpp)
target_compile_features(test-camera-registration PRIVATE cxx_std_17)
target_link_libraries(test-camera-registration
	PRIVATE lain::camera lain::meta lain::testing lain::warnings Catch2::Catch2WithMain)
target_compile_definitions(test-camera-registration PRIVATE
	LAIN_CAMERA_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
	LAIN_CAMERA_GOLDEN_OUT="${CMAKE_CURRENT_BINARY_DIR}")
catch_discover_tests(test-camera-registration TEST_PREFIX "(camera) ")

# A build that can pose a board but not refine a registration: a pose solver and no refiner.
add_executable(test-camera-norefiner test_norefiner.cpp)
target_compile_features(test-camera-norefiner PRIVATE cxx_std_17)
target_link_libraries(test-camera-norefiner PRIVATE lain::camera lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-norefiner TEST_PREFIX "(camera) ")
