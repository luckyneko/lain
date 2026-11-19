# lain::mesh — CPU triangle meshes, as lain::image is CPU rasters (WORK.md M15 slice 2b): an indexed mesh,
# its parts by material, each material as its file states it, and a mesh clip, a run of meshes at a frame
# rate. Names no file format: lain::io::mesh loads them, and codec plugins under plugins/io/mesh parse.

add_library(mesh STATIC
	src/mesh.cpp
)
add_library(lain::mesh ALIAS mesh)

target_compile_features(mesh PUBLIC cxx_std_17)
target_include_directories(mesh PUBLIC include)
target_link_libraries(mesh
	PUBLIC lain::core lain::image lain::math lain::media
	PRIVATE lain::string lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
