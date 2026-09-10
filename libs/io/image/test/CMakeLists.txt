# lain::io::image tests (Catch2). enable_testing() + addcatch2 are at the project root.
# Exercises the decode/load seam with a fake in-memory reader — no real codec, no
# driver. load() reads real temp files (the production path).

add_executable(test-io-image
	test_load.cpp
	test_save.cpp
	test_sequence.cpp
)

target_compile_features(test-io-image PRIVATE cxx_std_17)
# lain::io: test_save reads the written file back via io::read (io::image links io PRIVATE).
target_link_libraries(test-io-image PRIVATE lain::io::image lain::io lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-image TEST_PREFIX "(io::image) ")
