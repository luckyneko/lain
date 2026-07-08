# lain::io::image::png tests (Catch2). Decodes embedded PNG fixtures through the plugin.

add_executable(test-io-image-png
	test_pngreader.cpp
)

target_compile_features(test-io-image-png PRIVATE cxx_std_17)
target_link_libraries(test-io-image-png PRIVATE lain::io::image::png lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-image-png TEST_PREFIX "(io::image::png) ")
