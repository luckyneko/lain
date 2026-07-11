# JSON codec plugin test (Catch2): the Value <-> JSON round-trip end to end, plus the reflection
# two-hop and file save/load. Links the plugin (JsonReader/JsonWriter) + lain::data for toValue.

add_executable(test-io-data-json
	test_json.cpp
)

target_compile_features(test-io-data-json PRIVATE cxx_std_17)
target_link_libraries(test-io-data-json PRIVATE lain::io::data::json lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-data-json TEST_PREFIX "(io::data::json) ")
