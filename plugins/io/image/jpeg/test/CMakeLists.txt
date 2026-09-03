# lain::io::image::jpeg tests (Catch2). Decodes embedded JPEG fixtures through the plugin.

add_executable(test-io-image-jpeg
	test_jpegcolor.cpp
	test_jpegreader.cpp
)

target_compile_features(test-io-image-jpeg PRIVATE cxx_std_17)
# test_jpegcolor.cpp includes the plugin's private header to drive the marker scan as a pure
# function, the same way the video plugin's colour-policy test does.
target_include_directories(test-io-image-jpeg PRIVATE ../src)
target_link_libraries(test-io-image-jpeg PRIVATE lain::io::image::jpeg lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-image-jpeg TEST_PREFIX "(io::image::jpeg) ")
