# lain::io::data seam tests (Catch2). Exercises the production decode/load + encode/save dispatch
# with FAKE in-memory codecs — the seam is verified without any real parser, exactly as intended.

add_executable(test-io-data
	test_seam.cpp
)

target_compile_features(test-io-data PRIVATE cxx_std_17)
target_link_libraries(test-io-data PRIVATE lain::io::data lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-data TEST_PREFIX "(io-data) ")
