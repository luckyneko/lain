# lain::flow::serialize tests (Catch2). GPU-free nodes on a plain Graph — the serialize layer is a
# test surface exercised without any live GUI or driver, exactly as intended.

add_executable(test-flow-serialize
	test_params.cpp
	test_graph.cpp
	test_dynamic.cpp
	test_group.cpp
	test_map.cpp
	test_loop.cpp
	test_identity.cpp
	test_payloadtype.cpp
	test_documentpaths.cpp
)

target_compile_features(test-flow-serialize PRIVATE cxx_std_17)
# lain::io::data::json: the pipeline test round-trips a graph through real JSON bytes.
# lain::testing: the shared-definition case runs two instances of one template through the
# ParallelScheduler, so it has to start the process task pool (flow links task PRIVATE).
target_link_libraries(test-flow-serialize PRIVATE
	lain::flow::serialize
	lain::io::data::json
	lain::testing
	lain::warnings
	Catch2::Catch2WithMain
)

catch_discover_tests(test-flow-serialize TEST_PREFIX "(flow-serialize) ")
