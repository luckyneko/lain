# lain::mesh tests (Catch2). Pure unit tests — no files.

add_executable(test-mesh
	test_mesh.cpp
)

target_compile_features(test-mesh PRIVATE cxx_std_17)
target_link_libraries(test-mesh PRIVATE lain::mesh lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-mesh TEST_PREFIX "(mesh) ")
