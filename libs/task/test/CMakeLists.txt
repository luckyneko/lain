# lain::task smoke tests (Catch2). enable_testing() + addcatch2 are done at the
# project root. Exercises the wrapper's production surface — emplace + precede +
# run — not Taskflow directly.

add_executable(test-task
	test_task.cpp
)

target_compile_features(test-task PRIVATE cxx_std_17)
target_link_libraries(test-task PRIVATE lain::task lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-task TEST_PREFIX "(task) ")
