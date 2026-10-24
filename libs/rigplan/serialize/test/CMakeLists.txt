# lain::rigplan::serialize tests (Catch2). Pure unit tests: documents as data::Values, no files.

add_executable(test-rigplan-serialize
	test_serialize.cpp
)

target_compile_features(test-rigplan-serialize PRIVATE cxx_std_17)
# The rig plan the rigplan tests share.
target_include_directories(test-rigplan-serialize PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-rigplan-serialize PRIVATE lain::rigplan::serialize lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-rigplan-serialize TEST_PREFIX "(rigplan::serialize) ")
