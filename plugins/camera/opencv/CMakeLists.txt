# lain::camera::opencv — the OpenCV-backed camera plugin: the board renderer and detector behind
# lain::camera::board's seams, the calibration estimator behind lain::camera::calibration's, and the
# probe proving the linked OpenCV is the approved build (ADR-0026). No OpenCV type crosses into lain's
# camera contracts.
#
# addOpenCV is included HERE, not at the root, so nothing is fetched unless this plugin is enabled —
# the rule addFFmpeg follows for the video plugin.

include(addOpenCV) # opencv_core et al (fetches the pinned prebuilt on demand)

add_library(camera-opencv STATIC
	src/build.cpp
	src/register.cpp
	src/charucoboard.cpp
	src/charucorenderer.cpp
	src/charucodetector.cpp
	src/opencvestimator.cpp
)
add_library(lain::camera::opencv ALIAS camera-opencv)

target_compile_features(camera-opencv PUBLIC cxx_std_17)
target_include_directories(camera-opencv PUBLIC include)
target_link_libraries(camera-opencv
	PUBLIC lain::camera # it IS a board Renderer and Detector, and registerBackend fills those registries
	PRIVATE opencv_core opencv_imgproc opencv_objdetect opencv_calib3d
	PRIVATE lain::image
	PRIVATE lain::warnings
)

# The generated lain::camera::registerCameraBackends() discovers the plugin through this list and
# derives its <lain/camera/opencv/register.h> include and registerBackend() call from the name.
set_property(GLOBAL APPEND PROPERTY LAIN_CAMERA_BACKENDS lain::camera::opencv)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
