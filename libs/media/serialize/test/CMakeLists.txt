# lain::media::serialize tests (Catch2). Pure unit — the synthetic FrameSource from the media tests
# stands in for a medium, so what is exercised is the document, not a decoder.

add_executable(test-media-serialize
	test_manifest.cpp
)

target_compile_features(test-media-serialize PRIVATE cxx_std_17)
target_include_directories(test-media-serialize PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-media-serialize PRIVATE lain::media::serialize lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-media-serialize TEST_PREFIX "(media::serialize) ")
