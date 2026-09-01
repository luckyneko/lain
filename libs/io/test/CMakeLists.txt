# lain::io tests (Catch2). enable_testing() + addcatch2 are at the project root.
# Reads real files from the temp dir (the production path) — no driver.

add_executable(test-io
	test_read.cpp
	test_write.cpp
	test_stream.cpp
	test_uri.cpp
)

target_compile_features(test-io PRIVATE cxx_std_17)
target_link_libraries(test-io PRIVATE lain::io lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io TEST_PREFIX "(io) ")
