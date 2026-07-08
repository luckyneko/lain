# Aggregator test (Catch2): registerImageCodecs() wires the build-enabled codecs.

add_executable(test-io-image-codecs
	test_codecs.cpp
)

target_compile_features(test-io-image-codecs PRIVATE cxx_std_17)
target_link_libraries(test-io-image-codecs PRIVATE lain::io::image::codecs lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-image-codecs TEST_PREFIX "(io::image::codecs) ")
