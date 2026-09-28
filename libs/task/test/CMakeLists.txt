# lain::task tests (Catch2). enable_testing() + addcatch2 are done at the project root.
#
# lain::task is an alias for multi, so there is no wrapper surface to exercise: these are
# characterisation tests of the three multi behaviours flow's lowering and lain::app's lifetime
# depend on. lain::testing is for the pool — two of the three cases are vacuous without workers.

add_executable(test-task
	test_task.cpp
)

target_compile_features(test-task PRIVATE cxx_std_17)
target_link_libraries(test-task PRIVATE lain::task lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-task TEST_PREFIX "(task) ")
