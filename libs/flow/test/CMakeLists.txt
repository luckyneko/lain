# lain::flow tests (Catch2). enable_testing() + addcatch2 are done at the project
# root. PortValue's GPU kinds use null acm:: handles, so this is a pure unit suite
# (no driver needed).

add_executable(test-flow
	test_portvalue.cpp
)

target_compile_features(test-flow PRIVATE cxx_std_17)
target_link_libraries(test-flow PRIVATE lain::flow lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-flow TEST_PREFIX "(flow) ")
