# lain::math tests (Catch2). enable_testing() + addcatch2 are done at the project
# root. Pure compile-time / behavioral checks over the typed GLM aliases — no driver.

add_executable(test-math
	test_types.cpp
)

target_compile_features(test-math PRIVATE cxx_std_17)
target_link_libraries(test-math PRIVATE lain::math lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-math TEST_PREFIX "(math) ")
