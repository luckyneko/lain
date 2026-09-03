# lain::io::image::png — the PNG codec plugin. A PngReader decoding via libpng into a
# CPU image::Image, registered into the io::image reader registry through registerCodec.
# A swappable satellite of the codec-free lain::io::image seam: libpng + zlib live only
# here (fetched by addLibPNG only because this plugin is enabled), never in core io.

include(addLibPNG) # PNG::png (fetches libpng + zlib on demand)

add_library(io-image-png STATIC
	src/pngcolor.cpp
	src/pngreader.cpp
	src/pngwriter.cpp
	src/register.cpp
)
add_library(lain::io::image::png ALIAS io-image-png)

target_compile_features(io-image-png PUBLIC cxx_std_17)
target_include_directories(io-image-png PUBLIC include)
target_link_libraries(io-image-png
	PUBLIC lain::io::image
	PRIVATE PNG::png lain::warnings
)

# Advertise this codec to the generated aggregator (plugins/io/image/CMakeLists.txt).
# The entry is the alias target *and* the namespace — the aggregator links it and, from
# the same string, derives its <lain/io/image/png/register.h> include and registerCodec()
# call. Appending here is all it takes for a new codec to join registerImageCodecs().
set_property(GLOBAL APPEND PROPERTY LAIN_IMAGE_CODECS lain::io::image::png)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
