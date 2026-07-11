# The serialization codec plugins + the generated aggregator.
#
# Each <fmt>/ subdir is a swappable codec satellite that, when its opt-out option is on, appends its
# alias-target/namespace to the global LAIN_DATA_CODECS list. This file then generates
# lain::io::data::codecs — one target that links the enabled set and exposes registerDataCodecs()
# calling each plugin's registerCodec(). Membership is build-discovered, so a new plugins/io/data/<fmt>/
# folder joins the aggregator with no edit here beyond its add_subdirectory guard. Mirrors
# plugins/io/image.

option(LAIN_IO_DATA_JSON "Build the JSON data codec plugin (nlohmann/json)" ON)
if(LAIN_IO_DATA_JSON)
	add_subdirectory(json)
endif()

# Turn the discovered codec list into the aggregator's #includes and registerCodec() calls. Each
# entry is both the link target and the namespace: "lain::io::data::json" -> include
# <lain/io/data/json/register.h> + call lain::io::data::json::registerCodec().
get_property(_lain_data_codecs GLOBAL PROPERTY LAIN_DATA_CODECS)

set(LAIN_DATA_CODEC_INCLUDES "")
set(LAIN_DATA_CODEC_CALLS "")
foreach(_codec IN LISTS _lain_data_codecs)
	string(REPLACE "::" "/" _codec_path "${_codec}")
	string(APPEND LAIN_DATA_CODEC_INCLUDES "#include <${_codec_path}/register.h>\n")
	string(APPEND LAIN_DATA_CODEC_CALLS "\t\t${_codec}::registerCodec();\n")
endforeach()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/registerdatacodecs.cpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/registerdatacodecs.cpp"
	@ONLY
)

add_library(io-data-codecs STATIC "${CMAKE_CURRENT_BINARY_DIR}/registerdatacodecs.cpp")
add_library(lain::io::data::codecs ALIAS io-data-codecs)

target_compile_features(io-data-codecs PUBLIC cxx_std_17)
target_include_directories(io-data-codecs PUBLIC include)
target_link_libraries(io-data-codecs
	PUBLIC lain::io::data
	PRIVATE ${_lain_data_codecs} lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
