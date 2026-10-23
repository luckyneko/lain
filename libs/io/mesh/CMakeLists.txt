# lain::io::mesh — the codec-free mesh-loader seam (WORK.md M15 slice 2b): the MeshReader interface, the
# MeshFolder a reader reads what a mesh names through, the reader registry (core::Factory<MeshReader>)
# and the load() / decode() / loadClip() facade. Holds NO parser: format readers are plugins under
# plugins/io/mesh/<fmt> that register into the registry, as io::image's codecs do. A nested sub-target
# of libs/io.

add_library(io-mesh STATIC
	src/load.cpp
)
add_library(lain::io::mesh ALIAS io-mesh)

target_compile_features(io-mesh PUBLIC cxx_std_17)
target_include_directories(io-mesh PUBLIC include)
target_link_libraries(io-mesh
	PUBLIC lain::mesh lain::memory lain::core lain::media
	PRIVATE lain::io lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
