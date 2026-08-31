# The runtime half of the licence gate (Catch2). It links the real FFmpeg and asks it what
# it is, so it also proves the shared library RESOLVES at run time — an executable that
# cannot find @rpath/libavutil does not launch, which is the check that keeps lain's only
# shared-linked dependency honest without asserting anything about rpath.

add_executable(test-io-video-ffmpeg
	test_build.cpp
)

target_compile_features(test-io-video-ffmpeg PRIVATE cxx_std_17)
target_link_libraries(test-io-video-ffmpeg PRIVATE lain::io::video::ffmpeg lain::warnings Catch2::Catch2WithMain)

# Windows has no rpath: the DLLs must sit beside the executable or it will not start.
lain_ffmpeg_stage_runtime(test-io-video-ffmpeg)

catch_discover_tests(test-io-video-ffmpeg TEST_PREFIX "(io::video::ffmpeg) ")
