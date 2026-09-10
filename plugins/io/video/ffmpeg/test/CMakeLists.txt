# The FFmpeg plugin's tests (Catch2): the reader driven over embedded fixtures through the
# production seam, the colour policy as a pure function, and the runtime half of the licence gate.
#
# The licence test links the real FFmpeg and asks it what it is, so it also proves the shared
# library RESOLVES at run time — an executable that cannot find @rpath/libavutil does not launch,
# which is the check that keeps lain's only shared-linked dependency honest without asserting
# anything about rpath.

add_executable(test-io-video-ffmpeg
	test_build.cpp
	test_colorpolicy.cpp
	test_encoderpolicy.cpp
	test_ffmpegreader.cpp
	test_ffmpegwriter.cpp
)

target_compile_features(test-io-video-ffmpeg PRIVATE cxx_std_17)
# The colour-policy test includes the plugin's private header (and FFmpeg's enums with it), the
# reader test reaches the production opener through lain::io::video.
target_include_directories(test-io-video-ffmpeg PRIVATE ../src)
target_link_libraries(test-io-video-ffmpeg
	PRIVATE lain::io::video::ffmpeg lain::io::video lain::media FFmpeg::avutil FFmpeg::avcodec
	PRIVATE lain::testing lain::warnings Catch2::Catch2WithMain
)

# Windows has no rpath: the DLLs must sit beside the executable or it will not start.
lain_ffmpeg_stage_runtime(test-io-video-ffmpeg)

catch_discover_tests(test-io-video-ffmpeg TEST_PREFIX "(io::video::ffmpeg) ")
