# lain::io::mesh tests (Catch2). Reads real files from a scratch folder (the production path), through a
# small test format, since the seam holds no parser.

add_executable(test-io-mesh
	test_load.cpp
)

target_compile_features(test-io-mesh PRIVATE cxx_std_17)
target_link_libraries(test-io-mesh PRIVATE lain::io::mesh lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-mesh TEST_PREFIX "(io::mesh) ")
