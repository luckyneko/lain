# lain::io::video::ffmpeg — the FFmpeg-backed video codec plugin. Today it is only the
# licence probe that proves the linked FFmpeg is LGPL-configured; the reader lands here
# beside it (WORK.md M10 slice 5), which is why the seam is a plugin from the start.
#
# addFFmpeg is included HERE, not at the root, so nothing is fetched unless this plugin
# is enabled — the same rule addLibPNG follows for the png codec.

include(addFFmpeg) # FFmpeg::avutil et al (fetches the pinned LGPL archive on demand)

add_library(io-video-ffmpeg STATIC
	src/build.cpp
)
add_library(lain::io::video::ffmpeg ALIAS io-video-ffmpeg)

target_compile_features(io-video-ffmpeg PUBLIC cxx_std_17)
target_include_directories(io-video-ffmpeg PUBLIC include)
target_link_libraries(io-video-ffmpeg
	PRIVATE FFmpeg::avutil lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
