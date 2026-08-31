# flowview unit tests (Catch2). Driver-free — no window, no device. Compiles the production
# undo.cpp / groupnav.cpp / canvasids.cpp directly (not copies), so the tests exercise the real
# UndoStack, the real group-navigation model and the real imnodes id mapping.

add_executable(test-flowview
	test_undo.cpp
	test_groupnav.cpp
	test_canvasids.cpp
	test_graphio.cpp
	test_mapscene.cpp
	test_pinkey.cpp
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/undo.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/groupnav.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/canvasids.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/graphio.cpp"
)

target_compile_features(test-flowview PRIVATE cxx_std_17)
target_include_directories(test-flowview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
# lain::flow::serialize: groupnav's layout tree is an EditorTree (the serialize layer's type).
# lain::image + lain::io::data::codecs: graphio names image::Image / ColorRGBf and reads/writes json.
# Still driver-free — graphio touches the file system, but no device and no window.
target_link_libraries(test-flowview PRIVATE
	lain::data lain::flow lain::flow::serialize lain::flow-example lain::image
	lain::io lain::io::data::codecs lain::io::image::codecs
	lain::warnings Catch2::Catch2WithMain
)

catch_discover_tests(test-flowview TEST_PREFIX "(flowview) ")
