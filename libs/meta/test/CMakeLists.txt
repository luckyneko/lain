# lain::meta tests (Catch2). Pure unit — enum reflection, no driver.

add_executable(test-meta
	test_meta.cpp
	test_typenames.cpp
	test_traits.cpp
	test_tostring.cpp
	test_typelist.cpp
)

target_compile_features(test-meta PRIVATE cxx_std_17)
target_link_libraries(test-meta PRIVATE lain::meta lain::core lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-meta TEST_PREFIX "(meta) ")
