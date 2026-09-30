# The OpenCV plugin's tests (Catch2): the runtime half of the ADR-0026 gate, the ChArUco renderer
# and detector through lain's production seams, and lain's projection checked against OpenCV's.
#
# The test links the real OpenCV and asks it what it is, so it also proves the shared libraries
# RESOLVE at run time — an executable that cannot find @rpath/libopencv_core does not launch. The
# pins it checks against come from cmake/addOpenCV.cmake, so the manifest gate and this test can
# never be describing two different builds.

add_executable(test-camera-opencv
	test_build.cpp
	test_charuco.cpp
	test_projectionagreement.cpp
)

string(REPLACE ";" " " _lain_opencv_modules "${LAIN_OPENCV_MODULES}")
target_compile_features(test-camera-opencv PRIVATE cxx_std_17)
target_compile_definitions(test-camera-opencv PRIVATE
	LAIN_OPENCV_PINNED_VERSION="${LAIN_OPENCV_VERSION}"
	LAIN_OPENCV_PINNED_MODULES="${_lain_opencv_modules}"
)
target_link_libraries(test-camera-opencv
	PRIVATE lain::camera::opencv
	# The tests themselves name OpenCV: to draw a camera's view of a board, and to ask OpenCV's
	# projection what lain's should agree with. The plugin links it privately.
	PRIVATE opencv_core opencv_imgproc opencv_calib3d opencv_objdetect
	PRIVATE lain::image
	PRIVATE lain::warnings Catch2::Catch2WithMain
)

# Windows has no rpath: the DLLs must sit beside the executable or it will not start — and test
# discovery runs it at BUILD time, so staging has to come first.
lain_opencv_stage_runtime(test-camera-opencv)

catch_discover_tests(test-camera-opencv TEST_PREFIX "(camera::opencv) ")
