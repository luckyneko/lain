# lain::camera::opencv — the OpenCV-backed camera plugin: the board renderer, detector and pose
# solver behind lain::camera::board's seams, the calibration estimator behind
# lain::camera::calibration's, the feature extractor (SIFT), matcher and geometry solver behind
# lain::camera::feature's, and the probe proving the linked OpenCV is the approved build (ADR-0026).
# No OpenCV type crosses into lain's camera contracts.
#
# addOpenCV is included HERE, not at the root, so nothing is fetched unless this plugin is enabled —
# the rule addFFmpeg follows for the video plugin.

include(addOpenCV) # opencv_core et al (fetches the pinned prebuilt on demand)
if(NOT LAIN_OPENCV_FOUND)
	# No prebuilt for this platform: build nothing, so the aggregator registers no backend. The
	# reason goes up to plugins/camera for the configure summary.
	message(STATUS "LAIN_CAMERA_OPENCV: not built, ${LAIN_OPENCV_UNAVAILABLE}")
	set(LAIN_OPENCV_UNAVAILABLE "${LAIN_OPENCV_UNAVAILABLE}" PARENT_SCOPE)
	return()
endif()

add_library(camera-opencv STATIC
	src/build.cpp
	src/register.cpp
	src/charucoboard.cpp
	src/charucorenderer.cpp
	src/charucodetector.cpp
	src/imageprep.cpp
	src/opencvestimator.cpp
	src/opencvgeometry.cpp
	src/opencvmatcher.cpp
	src/opencvposesolver.cpp
	src/siftextractor.cpp
)
add_library(lain::camera::opencv ALIAS camera-opencv)

target_compile_features(camera-opencv PUBLIC cxx_std_17)
target_include_directories(camera-opencv PUBLIC include)
target_link_libraries(camera-opencv
	PUBLIC lain::camera # it IS a board Renderer and Detector, and registerBackend fills those registries
	PRIVATE opencv_core opencv_imgproc opencv_objdetect opencv_calib3d opencv_features2d opencv_flann
	PRIVATE lain::image
	PRIVATE lain::warnings
)

# The generated lain::camera::registerCameraBackends() discovers the plugin through this list and
# derives its <lain/camera/opencv/register.h> include and registerBackend() call from the name.
set_property(GLOBAL APPEND PROPERTY LAIN_CAMERA_BACKENDS lain::camera::opencv)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
