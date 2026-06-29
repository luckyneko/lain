# lain::log tests (Catch2). Pure unit — exercises the wrapper (level mapping, the
# typed fmt front-ends, the brace-safe emit seam). No spdlog in the test TU, which
# also confirms the backend stays hidden behind lain::log.

add_executable(test-log
	test_log.cpp
)

target_compile_features(test-log PRIVATE cxx_std_17)
target_link_libraries(test-log PRIVATE lain::log lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-log TEST_PREFIX "(log) ")
