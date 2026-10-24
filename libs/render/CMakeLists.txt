# lain::render — the raster renderer over archimedes (WORK.md M15, ADR-0028): a render scene of
# meshes, materials and lights, drawn through a pinhole, offscreen and read back, or into a viewport a
# pane shows with overlay lines over it. It knows nothing of rig plans; a rig plan's realisation at one
# frame is turned into a render scene elsewhere.
#
# Its GLSL is compiled at build time and embedded (cmake/lainShaders.cmake), so nothing is staged
# beside an executable that links it.

include(lainShaders)

add_library(render STATIC
	src/scene.cpp
	src/view.cpp
	src/renderer.cpp
	src/viewport.cpp
	src/capture.cpp
)
add_library(lain::render ALIAS render)

lain_embed_shaders(render NAMESPACE lain::render SHADERS
	shaders/scene.vert
	shaders/scene.frag
	shaders/lines.vert
	shaders/lines.frag
	INCLUDES shaders/frame.glsl
)

target_compile_features(render PUBLIC cxx_std_17)
target_include_directories(render PUBLIC include)
target_link_libraries(render
	PUBLIC archimedes  # acm::Device, which a renderer is made on (renderer.h)
	PUBLIC lain::camera # camera::Intrinsics and ImageGeometry describe a view (view.h)
	PUBLIC lain::image lain::math lain::mesh
	PRIVATE lain::string lain::task lain::warnings # lain::task: a ray table is built on the process pool (capture.h)
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
