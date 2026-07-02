# lain::core tests (Catch2). enable_testing() + addcatch2 are at the project root.
# Pure unit tests — no driver, no sleeps.

add_executable(test-core
	test_factory.cpp
	test_time.cpp
	test_version.cpp
)

target_compile_features(test-core PRIVATE cxx_std_17)
target_link_libraries(test-core PRIVATE lain::core lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-core TEST_PREFIX "(core) ")
