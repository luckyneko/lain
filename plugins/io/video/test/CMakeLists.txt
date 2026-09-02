# Aggregator test (Catch2): registerVideoCodecs() wires exactly the build-enabled codecs — of
# which there are legitimately zero in the default configuration.

list(LENGTH _lain_video_codecs _lain_video_codec_count)

add_executable(test-io-video-codecs
	test_codecs.cpp
)

target_compile_features(test-io-video-codecs PRIVATE cxx_std_17)
target_compile_definitions(test-io-video-codecs PRIVATE LAIN_VIDEO_CODEC_COUNT=${_lain_video_codec_count})
target_link_libraries(test-io-video-codecs PRIVATE lain::io::video::codecs lain::warnings Catch2::Catch2WithMain)

# Windows has no rpath: with the FFmpeg plugin enabled its DLLs must sit beside the executable.
if(LAIN_IO_VIDEO_FFMPEG)
	lain_ffmpeg_stage_runtime(test-io-video-codecs)
endif()

catch_discover_tests(test-io-video-codecs TEST_PREFIX "(io::video::codecs) ")
