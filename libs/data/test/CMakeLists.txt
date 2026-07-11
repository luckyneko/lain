# lain::data tests (Catch2). Pure unit tests — no driver, no sleeps.

add_executable(test-data
	test_value.cpp
	test_serialize.cpp
	test_macro_variant.cpp
)

target_compile_features(test-data PRIVATE cxx_std_17)
target_link_libraries(test-data PRIVATE lain::data lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-data TEST_PREFIX "(data) ")
