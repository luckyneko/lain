# lain::flow tests (Catch2). enable_testing() + addcatch2 are done at the project root.
# All pure unit — no driver, no SKIP: the example nodes are CPU now (lain::image::Image),
# and test_portvalue only needs null acm:: handles to prove the payload-agnostic slot.

add_executable(test-flow
	test_portvalue.cpp
	test_collection.cpp
	test_evaluation.cpp
	test_graph.cpp
	test_scheduler.cpp
	test_edit.cpp
	test_describe.cpp
	test_nodes.cpp
	test_sequencenodes.cpp
	test_params.cpp
	test_boundary.cpp
	test_group.cpp
	test_map.cpp
	test_nestedrun.cpp
	test_groupsync.cpp
	test_groupedit.cpp
	test_dynamicports.cpp
	test_serialize_support.cpp
	test_incremental.cpp
	test_conditional.cpp
	test_controlnodes.cpp
)

target_compile_features(test-flow PRIVATE cxx_std_17)
# lain::task: the scheduler tests name task::Executor (flow links it PRIVATEly).
# lain::flow-example: the example-node tests (pure CPU, pull in lain::image transitively).
# archimedes + Vulkan::Loader: test_portvalue names null acm::Texture/Buffer to prove a GPU
# handle rides the payload-agnostic slot (the loader resolves archimedes' vk* at link).
target_link_libraries(test-flow PRIVATE
	lain::flow
	lain::flow-example
	lain::io::image # the LoadImageNode tests register a fake reader / call readerRegistry()
	lain::task
	archimedes
	lain::warnings
	Vulkan::Loader
	Catch2::Catch2WithMain
)

catch_discover_tests(test-flow TEST_PREFIX "(flow) ")
