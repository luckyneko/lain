# lain::image tests (Catch2). Pure unit — no driver, no sleeps.

add_executable(test-image
	test_image.cpp
	test_color.cpp
	test_colormath.cpp
	test_imageview.cpp
	test_traverse.cpp
)

target_compile_features(test-image PRIVATE cxx_std_17)
target_link_libraries(test-image PRIVATE lain::image lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-image TEST_PREFIX "(image) ")
