# lain::camera::flow — the camera work as node kinds (M9 slice 1, ADR-0016): a board specification,
# its rendering, detection in one frame, board calibration over footage, and the model a calibration
# found. A separate target over lain::camera + lain::flow by the boundary rule lain::media::serialize
# follows: camera stays free of the graph engine, and flow core names no camera type.
#
# The namespace is lain::camera, as lain::media::serialize's is lain::media. A lain::camera::flow
# NAMESPACE would make every `flow::` written inside lain::camera resolve to it rather than to
# lain::flow.
#
# Node kinds register only when a backend can run them (registerCameraNodes); the port types are
# vocabulary and register always, so a boundary pin of a camera type still loads and saves.

add_library(camera-flow STATIC
	src/boardspecificationnode.cpp
	src/renderboardnode.cpp
	src/detectboardnode.cpp
	src/calibratecameranode.cpp
	src/cameramodelnode.cpp
	src/register.cpp
)
add_library(lain::camera::flow ALIAS camera-flow)

target_compile_features(camera-flow PUBLIC cxx_std_17)
target_include_directories(camera-flow PUBLIC include)
target_link_libraries(camera-flow
	PUBLIC lain::camera # the nodes carry its values
	PUBLIC lain::flow	# and are flow::Nodes
	PRIVATE lain::log	# a board specification that cannot be built says why
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
