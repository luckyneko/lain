# lain::media tests (Catch2). Pure unit — no driver, no files, no third-party codec: a
# synthetic FrameSource (testsource.h) stands in for a medium, so what is exercised is the
# model, not a decoder.

add_executable(test-media
	test_framespec.cpp
	test_framesource.cpp
	test_framesequence.cpp
	test_operations.cpp
)

target_compile_features(test-media PRIVATE cxx_std_17)
target_link_libraries(test-media PRIVATE lain::media lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-media TEST_PREFIX "(media) ")
