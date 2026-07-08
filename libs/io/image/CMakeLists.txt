# lain::io::image — the codec-free image-loader seam: the ImageReader interface, the
# reader registry (core::Factory<ImageReader>), and the load()/decode() facade. Holds
# NO third-party codec. Real format readers (turbojpeg, png, tiff) are separate codec
# plugins under plugins/io/image/<fmt> that depend on this seam and register into the
# registry (WORK.md M3). A nested sub-target of libs/io.

add_library(io-image STATIC
	src/load.cpp
	src/save.cpp
)
add_library(lain::io::image ALIAS io-image)

target_compile_features(io-image PUBLIC cxx_std_17)
target_include_directories(io-image PUBLIC include)
target_link_libraries(io-image
	PUBLIC lain::image lain::memory lain::core
	PRIVATE lain::io lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
