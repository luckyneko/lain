# lain::io::image — the codec-free image-loader seam: the ImageReader interface, the
# reader registry (core::Factory<ImageReader>), and the load()/decode() facade. Holds
# NO third-party codec. Real format readers (turbojpeg, png, tiff) are separate codec
# plugins under plugins/io/image/<fmt> that depend on this seam and register into the
# registry (WORK.md M3). A nested sub-target of libs/io.
#
# It also owns open() (WORK.md M10): the IMAGE medium's frame-sequence opener, peer of
# io::video::open. Each medium's opener lives in its own io seam, so lain::media — which owns the
# FrameSequence itself and depends on no io — never depends on every medium.

add_library(io-image STATIC
	src/load.cpp
	src/save.cpp
	src/open.cpp
)
add_library(lain::io::image ALIAS io-image)

target_compile_features(io-image PUBLIC cxx_std_17)
target_include_directories(io-image PUBLIC include)
target_link_libraries(io-image
	PUBLIC lain::image lain::memory lain::core
	PUBLIC lain::media				# open() returns a media::FrameSequence (open.h)
	PRIVATE lain::io lain::log lain::warnings
	PRIVATE lain::string				# the <key> pattern a numbered still sequence matches by (open.cpp)
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
