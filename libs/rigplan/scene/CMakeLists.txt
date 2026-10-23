# lain::rigplan::scene — a rig plan as lain::render draws it (WORK.md M15 slice 3c): its assets loaded
# once, and its realisation at a frame turned into a render scene. A separate target, so lain::rigplan
# stays free of the GPU and of io; built beside lain::render, which it needs.
#
# Its namespace is lain::rigplan, as lain::rigplan::serialize's is.

add_library(rigplan-scene STATIC
	src/scene.cpp
)
add_library(lain::rigplan::scene ALIAS rigplan-scene)

target_compile_features(rigplan-scene PUBLIC cxx_std_17)
target_include_directories(rigplan-scene PUBLIC include)
target_link_libraries(rigplan-scene
	PUBLIC lain::rigplan lain::render lain::mesh lain::image
	PRIVATE lain::io::image lain::io::mesh # the pictures and meshes a plan names
	PRIVATE lain::log					   # the untagged picture taken as sRGB
	PRIVATE lain::string lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
