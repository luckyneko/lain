# lain::string tests (Catch2). Pure unit — format() and the lain-type formatter path, and the
# <key> pattern system over the real pattern.cpp.

add_executable(test-string
	test_string.cpp
	test_pattern.cpp
)

target_compile_features(test-string PRIVATE cxx_std_17)
target_link_libraries(test-string PRIVATE lain::string lain::core lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-string TEST_PREFIX "(string) ")
