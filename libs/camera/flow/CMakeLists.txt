# lain::camera::flow — the camera work as node kinds (M9, ADR-0016): a board specification, its
# rendering, detection in one frame, board calibration over footage, the model a calibration found,
# and fixed-camera board registration over a rig's footage. A separate target over lain::camera + lain::flow by the boundary rule lain::media::serialize
# follows: camera stays free of the graph engine, and flow core names no camera type.
#
# The namespace is lain::camera, as lain::media::serialize's is lain::media. A lain::camera::flow
# NAMESPACE would make every `flow::` written inside lain::camera resolve to it rather than to
# lain::flow.
#
# Node kinds and port types are vocabulary and register always (registerCameraNodes,
# registerCameraPortTypes), so a camera document loads and saves whole in a build with no backend;
# which kinds are OFFERED follows the backends (availableCameraNodeKeys).

add_library(camera-flow STATIC
	src/boardspecificationnode.cpp
	src/renderboardnode.cpp
	src/detectboardnode.cpp
	src/calibratecameranode.cpp
	src/cameramodelnode.cpp
	src/registercamerasnode.cpp
	src/register.cpp
)
add_library(lain::camera::flow ALIAS camera-flow)

target_compile_features(camera-flow PUBLIC cxx_std_17)
target_include_directories(camera-flow PUBLIC include)
target_link_libraries(camera-flow
	PUBLIC lain::camera # the nodes carry its values
	PUBLIC lain::flow	# and are flow::Nodes
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
