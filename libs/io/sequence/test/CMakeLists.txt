# lain::io::sequence tests (Catch2). The registry is what is under test, so the media are fakes;
# the real seams are exercised by their own tests.

add_executable(test-io-sequence
	test_open.cpp
)

target_compile_features(test-io-sequence PRIVATE cxx_std_17)
target_link_libraries(test-io-sequence PRIVATE lain::io::sequence lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-sequence TEST_PREFIX "(io::sequence) ")
