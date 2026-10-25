# lain::rigplan tests (Catch2). Pure unit tests — no driver, no files.

add_executable(test-rigplan
	test_specification.cpp
	test_rigplan.cpp
	test_realisation.cpp
	test_environment.cpp
	test_sensor.cpp
)

target_compile_features(test-rigplan PRIVATE cxx_std_17)
target_link_libraries(test-rigplan PRIVATE lain::rigplan lain::testing lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-rigplan TEST_PREFIX "(rigplan) ")
