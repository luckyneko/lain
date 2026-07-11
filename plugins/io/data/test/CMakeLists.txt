# Aggregator test (Catch2): registerDataCodecs() wires the build-enabled codecs.

add_executable(test-io-data-codecs
	test_codecs.cpp
)

target_compile_features(test-io-data-codecs PRIVATE cxx_std_17)
target_link_libraries(test-io-data-codecs PRIVATE lain::io::data::codecs lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-data-codecs TEST_PREFIX "(io::data::codecs) ")
