# lain::flow tests (Catch2). enable_testing() + addcatch2 are done at the project
# root. The PortValue/graph/scheduler suites are pure unit (no driver); the [gpu]
# node test creates a real device and SKIPs when none is present.

add_executable(test-flow
	test_portvalue.cpp
	test_graph.cpp
	test_scheduler.cpp
	test_edit.cpp
	test_describe.cpp
	test_gpu_node.cpp
)

target_compile_features(test-flow PRIVATE cxx_std_17)
# lain::task: the scheduler tests name task::Executor (flow links it PRIVATEly).
# lain::flow-example + Vulkan::Loader: the [gpu] test creates a real device, so
# the executable must resolve archimedes' vk* symbols via the loader.
target_link_libraries(test-flow PRIVATE
	lain::flow
	lain::flow-example
	lain::task
	lain::warnings
	Vulkan::Loader
	Catch2::Catch2WithMain
)

catch_discover_tests(test-flow TEST_PREFIX "(flow) ")
