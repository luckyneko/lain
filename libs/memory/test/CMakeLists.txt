# lain::memory tests (Catch2). enable_testing() + addcatch2 are at the project root.
# Pure unit tests — no driver, no sleeps.

add_executable(test-memory
	test_alloc.cpp
	test_buffer.cpp
)

target_compile_features(test-memory PRIVATE cxx_std_17)
target_link_libraries(test-memory PRIVATE lain::memory lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-memory TEST_PREFIX "(memory) ")
