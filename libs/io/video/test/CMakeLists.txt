# lain::io::video tests (Catch2). Exercises the seam with a fake VideoReader — no codec, no
# FFmpeg — over real temp files, so the production open path runs in the default build.

add_executable(test-io-video
	test_open.cpp
)

target_compile_features(test-io-video PRIVATE cxx_std_17)
target_link_libraries(test-io-video PRIVATE lain::io::video lain::io lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-video TEST_PREFIX "(io::video) ")

# The empty-registry case needs a process with nothing registered in it, which the executable
# above cannot provide (a core::Factory only grows). Its own binary is cheaper than an ordering
# rule, and it is the DEFAULT build's behaviour, so it deserves to be unmissable.
add_executable(test-io-video-nocodec
	test_nocodec.cpp
)

target_compile_features(test-io-video-nocodec PRIVATE cxx_std_17)
target_link_libraries(test-io-video-nocodec PRIVATE lain::io::video lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-video-nocodec TEST_PREFIX "(io::video) ")
