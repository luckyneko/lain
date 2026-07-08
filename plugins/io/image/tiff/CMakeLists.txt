# lain::io::image::tiff — the TIFF codec plugin. A TiffReader decoding via libtiff into a
# CPU image::Image, registered into the io::image reader registry through registerCodec. A
# swappable satellite of the codec-free lain::io::image seam: libtiff + zlib live only here.

include(addLibTIFF) # TIFF::tiff (fetches libtiff + zlib on demand)

add_library(io-image-tiff STATIC
	src/tiffreader.cpp
)
add_library(lain::io::image::tiff ALIAS io-image-tiff)

target_compile_features(io-image-tiff PUBLIC cxx_std_17)
target_include_directories(io-image-tiff PUBLIC include)
target_link_libraries(io-image-tiff
	PUBLIC lain::io::image
	PRIVATE TIFF::tiff lain::log lain::warnings
)

# Advertise this codec to the generated aggregator (see plugins/io/image/CMakeLists.txt).
set_property(GLOBAL APPEND PROPERTY LAIN_IMAGE_CODECS lain::io::image::tiff)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
