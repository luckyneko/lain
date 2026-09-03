# lain::io::video::ffmpeg — the FFmpeg-backed video backend plugin: the reader that demuxes and
# decodes a container into media frames, plus the licence probe that proves the linked FFmpeg is
# LGPL-configured. Registered into lain::io::video's reader registry through registerCodec, so
# nothing in the codec-free seam names FFmpeg (WORK.md M10 slice 5).
#
# addFFmpeg is included HERE, not at the root, so nothing is fetched unless this plugin
# is enabled — the same rule addLibPNG follows for the png codec.

include(addFFmpeg) # FFmpeg::avutil et al (fetches the pinned LGPL archive on demand)

add_library(io-video-ffmpeg STATIC
	src/aviobridge.cpp
	src/build.cpp
	src/colorpolicy.cpp
	src/encoderpolicy.cpp
	src/ffmpegreader.cpp
	src/ffmpegwriter.cpp
	src/register.cpp
)
add_library(lain::io::video::ffmpeg ALIAS io-video-ffmpeg)

target_compile_features(io-video-ffmpeg PUBLIC cxx_std_17)
target_include_directories(io-video-ffmpeg PUBLIC include)
target_link_libraries(io-video-ffmpeg
	PUBLIC lain::io::video			# it IS a VideoReader, and registerCodec fills that registry
	PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale
	PRIVATE lain::io lain::log lain::warnings
)

# Advertise this backend to the generated aggregator (plugins/io/video/CMakeLists.txt). The entry
# is the alias target *and* the namespace: the aggregator links it and, from the same string,
# derives its <lain/io/video/ffmpeg/register.h> include and registerCodec() call.
set_property(GLOBAL APPEND PROPERTY LAIN_VIDEO_CODECS lain::io::video::ffmpeg)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
