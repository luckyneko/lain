# Camera plugins — the third-party-backed implementations behind lain's camera contracts (ADR-0016)
# — and the generated aggregator that registers them.
#
# The AGGREGATOR is built either way, and its registerCameraBackends() is then a no-op. The camera node
# kinds still register (a kind is vocabulary, ADR-0016 amended, the same rule as video's), so a camera
# document loads whole and its nodes report the missing backend when run; only the menu follows the
# backends.
#
# OpenCV is a fetched BINARY (ADR-0026), so the option defaults ON only for a top-level build. A
# bundled lain leaves it OFF, so a consumer's configure never downloads a binary it did not ask for,
# nor fails on that download. The option is what you want; whether this platform can have it is
# addOpenCV's answer, and an unavailable plugin is not built.

option(LAIN_CAMERA_OPENCV "Build the OpenCV camera plugin (Apache-2.0, fetched prebuilt)" ${LAIN_NOT_SUBPROJECT})
if(LAIN_CAMERA_OPENCV)
	add_subdirectory(opencv)
endif()
set(_lain_opencv_summary "OpenCV camera backend (Apache-2.0, fetched prebuilt)")
if(LAIN_OPENCV_UNAVAILABLE)
	string(APPEND _lain_opencv_summary ": not built, ${LAIN_OPENCV_UNAVAILABLE}")
endif()
add_feature_info(LAIN_CAMERA_OPENCV "TARGET lain::camera::opencv" "${_lain_opencv_summary}")

# Ceres is BUILT from source (ADR-0017), but heavy enough that a bundled lain should not compile it
# unasked, so it defaults the way a fetched prebuilt does. It is available wherever lain builds.
option(LAIN_CAMERA_CERES "Build the Ceres camera plugin (BSD-3-Clause, built from source; Eigen MPL-2.0)" ${LAIN_NOT_SUBPROJECT})
if(LAIN_CAMERA_CERES)
	add_subdirectory(ceres)
endif()
add_feature_info(LAIN_CAMERA_CERES "TARGET lain::camera::ceres" "Ceres camera registration backend (BSD-3-Clause, built from source)")

# Turn the discovered plugin list into the aggregator's #includes and registerBackend() calls. Each
# entry is both the link target and the namespace: "lain::camera::opencv" ->
# include <lain/camera/opencv/register.h> + call lain::camera::opencv::registerBackend().
get_property(_lain_camera_backends GLOBAL PROPERTY LAIN_CAMERA_BACKENDS)

set(LAIN_CAMERA_BACKEND_INCLUDES "")
set(LAIN_CAMERA_BACKEND_CALLS "")
foreach(_backend IN LISTS _lain_camera_backends)
	string(REPLACE "::" "/" _backend_path "${_backend}")
	string(APPEND LAIN_CAMERA_BACKEND_INCLUDES "#include <${_backend_path}/register.h>\n")
	string(APPEND LAIN_CAMERA_BACKEND_CALLS "\t\t${_backend}::registerBackend();\n")
endforeach()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/registercamerabackends.cpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/registercamerabackends.cpp"
	@ONLY
)

add_library(camera-backends STATIC "${CMAKE_CURRENT_BINARY_DIR}/registercamerabackends.cpp")
add_library(lain::camera::backends ALIAS camera-backends)

target_compile_features(camera-backends PUBLIC cxx_std_17)
target_include_directories(camera-backends PUBLIC include)
target_link_libraries(camera-backends
	PUBLIC lain::camera
	PRIVATE ${_lain_camera_backends} lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
