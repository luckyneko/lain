# The video codec plugins + the generated aggregator. Mirrors plugins/io/image, with one
# difference that is not cosmetic: FFmpeg is a fetched BINARY (ADR-0019), so the option defaults ON
# only for a top-level build. A bundled lain leaves it OFF, so a consumer's configure never
# downloads a binary it did not ask for, nor fails on that download. The option is what you want;
# whether this platform can have it is addFFmpeg's answer, and an unavailable plugin is not built.
#
# The AGGREGATOR is built either way, and its registerVideoCodecs() is then a no-op. That is
# deliberate: the seam it wires into (lain::io::video) is unconditional too, so a build without a
# codec answers "no video capability" for an .mp4 instead of losing the vocabulary to name one.

option(LAIN_IO_VIDEO_FFMPEG "Build the FFmpeg video codec plugin (LGPL, fetched prebuilt)" ${LAIN_NOT_SUBPROJECT})
if(LAIN_IO_VIDEO_FFMPEG)
	add_subdirectory(ffmpeg)
endif()
set(_lain_ffmpeg_summary "FFmpeg video codecs (LGPL, fetched prebuilt)")
if(LAIN_FFMPEG_UNAVAILABLE)
	string(APPEND _lain_ffmpeg_summary ": not built, ${LAIN_FFMPEG_UNAVAILABLE}")
endif()
add_feature_info(LAIN_IO_VIDEO_FFMPEG "TARGET lain::io::video::ffmpeg" "${_lain_ffmpeg_summary}")

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
