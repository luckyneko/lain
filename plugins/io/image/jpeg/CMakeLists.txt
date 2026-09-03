# lain::io::image::jpeg — the JPEG codec plugin. A JpegReader decoding via stb_image into a
# CPU image::Image, registered into the io::image reader registry through registerCodec. A
# swappable satellite of the codec-free lain::io::image seam: stb lives only here. stb was
# chosen over libjpeg-turbo because JPEG is 8-bit (the U16 reason for real codecs doesn't
# apply) and stb vendors trivially where turbojpeg refuses add_subdirectory; a turbojpeg
# swap can replace it behind this seam later (WORK.md M3).

include(addStb) # stb::image (fetches stb, compiles its impl in a non-strict TU)

add_library(io-image-jpeg STATIC
	src/jpegcolor.cpp
	src/jpegreader.cpp
	src/jpegwriter.cpp
	src/register.cpp
)
add_library(lain::io::image::jpeg ALIAS io-image-jpeg)

target_compile_features(io-image-jpeg PUBLIC cxx_std_17)
target_include_directories(io-image-jpeg PUBLIC include)
target_link_libraries(io-image-jpeg
	PUBLIC lain::io::image
	PRIVATE stb::image lain::log lain::warnings
)

# Advertise this codec to the generated aggregator (see plugins/io/image/CMakeLists.txt).
set_property(GLOBAL APPEND PROPERTY LAIN_IMAGE_CODECS lain::io::image::jpeg)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
