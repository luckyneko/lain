# Aggregator test (Catch2): registerDataCodecs() wires the build-enabled codecs.

add_executable(test-io-data-codecs
	test_codecs.cpp
)

target_compile_features(test-io-data-codecs PRIVATE cxx_std_17)
target_link_libraries(test-io-data-codecs PRIVATE lain::io::data::codecs lain::warnings Catch2::Catch2WithMain)

# Whether this build has the JSON codec, so the test asserts exactly that, both ways.
if(TARGET lain::io::data::json)
	target_compile_definitions(test-io-data-codecs PRIVATE LAIN_HAS_JSON=1)
else()
	target_compile_definitions(test-io-data-codecs PRIVATE LAIN_HAS_JSON=0)
endif()

catch_discover_tests(test-io-data-codecs TEST_PREFIX "(io::data::codecs) ")
