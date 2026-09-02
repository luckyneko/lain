# The video codec plugins + the generated aggregator. Mirrors plugins/io/image, with one
# difference that is not cosmetic: video is OPT-IN (ADR-0019). FFmpeg is a licence exception
# rather than an ordinary dependency, and an all-disabled configure must resolve nothing at all —
# so this option defaults OFF where the image codecs default ON.
#
# The AGGREGATOR is built either way, and its registerVideoCodecs() is then a no-op. That is
# deliberate: the seam it wires into (lain::io::video) is unconditional too, so a build without a
# codec answers "no video capability" for an .mp4 instead of losing the vocabulary to name one.

option(LAIN_IO_VIDEO_FFMPEG "Build the FFmpeg video codec plugin (LGPL, fetched)" OFF)
if(LAIN_IO_VIDEO_FFMPEG)
	add_subdirectory(ffmpeg)
endif()

# Turn the discovered codec list into the aggregator's #includes and registerCodec() calls. Each
# entry is both the link target and the namespace: "lain::io::video::ffmpeg" ->
# include <lain/io/video/ffmpeg/register.h> + call lain::io::video::ffmpeg::registerCodec().
get_property(_lain_video_codecs GLOBAL PROPERTY LAIN_VIDEO_CODECS)

set(LAIN_VIDEO_CODEC_INCLUDES "")
set(LAIN_VIDEO_CODEC_CALLS "")
foreach(_codec IN LISTS _lain_video_codecs)
	string(REPLACE "::" "/" _codec_path "${_codec}")
	string(APPEND LAIN_VIDEO_CODEC_INCLUDES "#include <${_codec_path}/register.h>\n")
	string(APPEND LAIN_VIDEO_CODEC_CALLS "\t\t${_codec}::registerCodec();\n")
endforeach()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/registervideocodecs.cpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/registervideocodecs.cpp"
	@ONLY
)

add_library(io-video-codecs STATIC "${CMAKE_CURRENT_BINARY_DIR}/registervideocodecs.cpp")
add_library(lain::io::video::codecs ALIAS io-video-codecs)

target_compile_features(io-video-codecs PUBLIC cxx_std_17)
target_include_directories(io-video-codecs PUBLIC include)
target_link_libraries(io-video-codecs
	PUBLIC lain::io::video
	PRIVATE ${_lain_video_codecs} lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
