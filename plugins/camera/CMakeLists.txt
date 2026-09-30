# Camera plugins — the optional, third-party-backed implementations behind lain's camera contracts
# (ADR-0016) — and the generated aggregator that registers them. Opt-in like video: an all-disabled
# configure must resolve nothing at all, which is M9's dependency-footprint rule, so every option
# here defaults OFF.
#
# The AGGREGATOR is built either way, and its registerCameraBackends() is then a no-op. Unlike video,
# where a missing codec leaves the node in place to report the missing capability, a camera node that
# has no backend cannot run at all, so the host reads the capability queries and registers none.

option(LAIN_CAMERA_OPENCV "Build the OpenCV camera plugin (Apache-2.0, fetched prebuilt)" OFF)
if(LAIN_CAMERA_OPENCV)
	add_subdirectory(opencv)
endif()

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
