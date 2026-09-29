# lain::camera::opencv — the OpenCV-backed camera plugin. Today it is only the probe that proves the
# linked OpenCV is the approved build (ADR-0026); M9 slice 1 adds the ChArUco detection and
# calibration adapters beside it, and no OpenCV type will cross into lain's camera contracts.
#
# addOpenCV is included HERE, not at the root, so nothing is fetched unless this plugin is enabled —
# the rule addFFmpeg follows for the video plugin.

include(addOpenCV) # opencv_core et al (fetches the pinned prebuilt on demand)

add_library(camera-opencv STATIC
	src/build.cpp
)
add_library(lain::camera::opencv ALIAS camera-opencv)

target_compile_features(camera-opencv PUBLIC cxx_std_17)
target_include_directories(camera-opencv PUBLIC include)
target_link_libraries(camera-opencv
	PRIVATE opencv_core
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
