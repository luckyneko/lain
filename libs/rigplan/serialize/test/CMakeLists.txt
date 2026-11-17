# lain::rigplan::serialize tests (Catch2). Pure unit tests: documents as data::Values, no files. Rig
# plans and catalogues in test_serialize.cpp, a capture's ground truth in test_truth.cpp, its manifest in
# test_capture.cpp.

add_executable(test-rigplan-serialize
	test_serialize.cpp
	test_truth.cpp
	test_capture.cpp
)

target_compile_features(test-rigplan-serialize PRIVATE cxx_std_17)
target_link_libraries(test-rigplan-serialize PRIVATE
	lain::rigplan::serialize
	lain::rigplan::testing # the rig plan the rigplan tests share
	lain::testing lain::warnings Catch2::Catch2WithMain
)

catch_discover_tests(test-rigplan-serialize TEST_PREFIX "(rigplan::serialize) ")
