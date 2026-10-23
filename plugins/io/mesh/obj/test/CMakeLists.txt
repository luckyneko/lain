# lain::io::mesh::obj tests (Catch2). OBJ and MTL files written to a scratch folder and loaded through the
# production io::mesh facade.

add_executable(test-io-mesh-obj
	test_obj.cpp
)

target_compile_features(test-io-mesh-obj PRIVATE cxx_std_17)
target_link_libraries(test-io-mesh-obj PRIVATE lain::io::mesh::obj lain::io::mesh lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-io-mesh-obj TEST_PREFIX "(io::mesh::obj) ")
