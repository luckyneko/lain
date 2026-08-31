# The video codec plugins. Mirrors plugins/io/image, with one difference that is not
# cosmetic: video is OPT-IN (ADR-0019). FFmpeg is a licence exception rather than an
# ordinary dependency, and an all-disabled configure must resolve nothing at all — so
# this option defaults OFF where the image codecs default ON.
#
# There is no aggregator here yet. The image side generates one because it has three
# codecs to wire into a registry; video has one plugin and, until slice 5, no registry
# for it to wire into. It arrives with the reader that needs it.

option(LAIN_IO_VIDEO_FFMPEG "Build the FFmpeg video codec plugin (LGPL, fetched)" OFF)
if(LAIN_IO_VIDEO_FFMPEG)
	add_subdirectory(ffmpeg)
endif()
