# flowview unit tests (Catch2). Driver-free — no window, no device. Compiles the production
# undo.cpp directly (not a copy), so the test exercises the real UndoStack.

add_executable(test-flowview
	test_undo.cpp
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/undo.cpp"
)

target_compile_features(test-flowview PRIVATE cxx_std_17)
target_include_directories(test-flowview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(test-flowview PRIVATE lain::data lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-flowview TEST_PREFIX "(flowview) ")
