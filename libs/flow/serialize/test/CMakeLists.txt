# lain::flow::serialize tests (Catch2). GPU-free nodes on a plain Graph — the serialize layer is a
# test surface exercised without any live GUI or driver, exactly as intended.

add_executable(test-flow-serialize
	test_params.cpp
)

target_compile_features(test-flow-serialize PRIVATE cxx_std_17)
target_link_libraries(test-flow-serialize PRIVATE lain::flow::serialize lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-flow-serialize TEST_PREFIX "(flow-serialize) ")
