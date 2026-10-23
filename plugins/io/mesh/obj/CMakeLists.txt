# lain::io::mesh::obj — the OBJ / MTL codec plugin: an ObjReader over tinyobjloader, registered into the
# io::mesh reader registry through registerCodec. tinyobjloader lives only here (fetched by
# addTinyObjLoader only because this plugin is enabled), never in core io::mesh. It reads an OBJ's MTL
# through the MeshFolder it is handed, so it opens no file itself.

include(addTinyObjLoader) # tinyobjloader::tinyobjloader

add_library(io-mesh-obj STATIC
	src/obj.cpp
)
add_library(lain::io::mesh::obj ALIAS io-mesh-obj)

target_compile_features(io-mesh-obj PUBLIC cxx_std_17)
target_include_directories(io-mesh-obj PUBLIC include)
target_link_libraries(io-mesh-obj
	PUBLIC lain::io::mesh
	PRIVATE tinyobjloader::tinyobjloader lain::warnings
)

# Advertise this codec to the generated aggregator (plugins/io/mesh/CMakeLists.txt).
set_property(GLOBAL APPEND PROPERTY LAIN_MESH_CODECS lain::io::mesh::obj)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
