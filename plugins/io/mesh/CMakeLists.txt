# The mesh codec plugins + the generated aggregator (WORK.md M15 slice 2c).
#
# Each <fmt>/ subdir is a swappable codec satellite that, when its opt-out option is on, appends its
# alias-target/namespace to the global LAIN_MESH_CODECS list. This file then generates
# lain::io::mesh::codecs — one target that links the enabled set and exposes registerMeshCodecs()
# calling each plugin's registerCodec(). Membership is build-discovered, so a new plugins/io/mesh/<fmt>/
# folder joins the aggregator with no edit here beyond its add_subdirectory guard. Mirrors
# plugins/io/data.

option(LAIN_IO_MESH_OBJ "Build the OBJ / MTL mesh codec plugin (tinyobjloader)" ON)
if(LAIN_IO_MESH_OBJ)
	add_subdirectory(obj)
endif()
add_feature_info(LAIN_IO_MESH_OBJ "TARGET lain::io::mesh::obj" "OBJ / MTL mesh codec (tinyobjloader)")

get_property(_lain_mesh_codecs GLOBAL PROPERTY LAIN_MESH_CODECS)

set(LAIN_MESH_CODEC_INCLUDES "")
set(LAIN_MESH_CODEC_CALLS "")
foreach(_codec IN LISTS _lain_mesh_codecs)
	string(REPLACE "::" "/" _codec_path "${_codec}")
	string(APPEND LAIN_MESH_CODEC_INCLUDES "#include <${_codec_path}/register.h>\n")
	string(APPEND LAIN_MESH_CODEC_CALLS "\t\t${_codec}::registerCodec();\n")
endforeach()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/registermeshcodecs.cpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/registermeshcodecs.cpp"
	@ONLY
)

add_library(io-mesh-codecs STATIC "${CMAKE_CURRENT_BINARY_DIR}/registermeshcodecs.cpp")
add_library(lain::io::mesh::codecs ALIAS io-mesh-codecs)

target_compile_features(io-mesh-codecs PUBLIC cxx_std_17)
target_include_directories(io-mesh-codecs PUBLIC include)
target_link_libraries(io-mesh-codecs
	PUBLIC lain::io::mesh
	PRIVATE ${_lain_mesh_codecs} lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
