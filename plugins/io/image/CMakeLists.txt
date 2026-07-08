# The image codec plugins + the generated aggregator.
#
# Each <fmt>/ subdir is a swappable codec satellite that, when its opt-out option is on,
# appends its alias-target/namespace to the global LAIN_IMAGE_CODECS list. This file then
# generates lain::io::image::codecs — one target that links the enabled set and exposes
# registerImageCodecs() calling each plugin's registerCodec(). The membership is
# build-discovered, so a new plugins/io/image/<fmt>/ folder joins the aggregator with no
# edit here beyond its add_subdirectory guard.

option(LAIN_IO_IMAGE_PNG "Build the PNG image codec plugin (libpng)" ON)
if(LAIN_IO_IMAGE_PNG)
	add_subdirectory(png)
endif()

option(LAIN_IO_IMAGE_TIFF "Build the TIFF image codec plugin (libtiff)" ON)
if(LAIN_IO_IMAGE_TIFF)
	add_subdirectory(tiff)
endif()

option(LAIN_IO_IMAGE_JPEG "Build the JPEG image codec plugin (stb_image)" ON)
if(LAIN_IO_IMAGE_JPEG)
	add_subdirectory(jpeg)
endif()

# Turn the discovered codec list into the aggregator's #includes and registerCodec()
# calls. Each entry is both the link target and the namespace: "lain::io::image::png" ->
# include <lain/io/image/png/register.h> + call lain::io::image::png::registerCodec().
get_property(_lain_codecs GLOBAL PROPERTY LAIN_IMAGE_CODECS)

set(LAIN_IMAGE_CODEC_INCLUDES "")
set(LAIN_IMAGE_CODEC_CALLS "")
foreach(_codec IN LISTS _lain_codecs)
	string(REPLACE "::" "/" _codec_path "${_codec}")
	string(APPEND LAIN_IMAGE_CODEC_INCLUDES "#include <${_codec_path}/register.h>\n")
	string(APPEND LAIN_IMAGE_CODEC_CALLS "\t\t${_codec}::registerCodec();\n")
endforeach()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/registerimagecodecs.cpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/registerimagecodecs.cpp"
	@ONLY
)

add_library(io-image-codecs STATIC "${CMAKE_CURRENT_BINARY_DIR}/registerimagecodecs.cpp")
add_library(lain::io::image::codecs ALIAS io-image-codecs)

target_compile_features(io-image-codecs PUBLIC cxx_std_17)
target_include_directories(io-image-codecs PUBLIC include)
target_link_libraries(io-image-codecs
	PUBLIC lain::io::image
	PRIVATE ${_lain_codecs} lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
