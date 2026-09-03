# lain::io::image::tiff tests (Catch2). Decodes embedded TIFF fixtures through the plugin.

add_executable(test-io-image-tiff
	test_tiffreader.cpp
)

target_compile_features(test-io-image-tiff PRIVATE cxx_std_17)
# The colour tests include the plugin's private header to ask what curve lain would write, the
# same way the video plugin's colour-policy test reaches its own policy.
target_include_directories(test-io-image-tiff PRIVATE ../src)
target_link_libraries(test-io-image-tiff PRIVATE lain::io::image::tiff TIFF::tiff lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-image-tiff TEST_PREFIX "(io::image::tiff) ")
