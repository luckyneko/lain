# lain::io::video — the codec-free video-loader seam: the VideoReader interface, the reader
# registry (core::Factory<VideoReader>), the container-extension claim, and the open() facade that
# turns a uri into a media::FrameSequence. Holds NO third-party codec; the FFmpeg-backed reader is
# a plugin under plugins/io/video/ffmpeg that depends on this seam and registers into the registry
# (WORK.md M10 slice 5). A nested sub-target of libs/io, peer of lain::io::image.
#
# IT IS BUILT UNCONDITIONALLY, unlike its plugin. That is the point: with no video codec compiled
# in, a document naming an .mp4 still routes here and is told the BUILD has no video capability,
# instead of falling through to the still opener and being told its file is not a directory
# (ADR-0019, amended). A capability can go missing; a vocabulary must not.

add_library(io-video STATIC
	src/open.cpp
	src/videosource.cpp
)
add_library(lain::io::video ALIAS io-video)

target_compile_features(io-video PUBLIC cxx_std_17)
target_include_directories(io-video PUBLIC include)
target_link_libraries(io-video
	PUBLIC lain::image lain::core
	PUBLIC lain::io					# a VideoReader is handed an io::ReadStream (reader.h)
	PUBLIC lain::media				# open() returns a media::FrameSequence
	PRIVATE lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
