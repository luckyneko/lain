# Aggregator test (Catch2): registerMeshCodecs() wires the build-enabled codecs.

add_executable(test-io-mesh-codecs
	test_codecs.cpp
)

target_compile_features(test-io-mesh-codecs PRIVATE cxx_std_17)
target_link_libraries(test-io-mesh-codecs PRIVATE lain::io::mesh::codecs lain::warnings Catch2::Catch2WithMain)

# Whether this build has the OBJ codec, so the test asserts exactly that, both ways.
if(TARGET lain::io::mesh::obj)
	target_compile_definitions(test-io-mesh-codecs PRIVATE LAIN_HAS_OBJ=1)
else()
	target_compile_definitions(test-io-mesh-codecs PRIVATE LAIN_HAS_OBJ=0)
endif()

catch_discover_tests(test-io-mesh-codecs TEST_PREFIX "(io::mesh::codecs) ")
