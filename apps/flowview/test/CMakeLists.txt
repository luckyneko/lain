# flowview unit tests (Catch2). Driver-free — no window, no device. Compiles the production
# undo.cpp / groupnav.cpp / canvasids.cpp directly (not copies), so the tests exercise the real
# UndoStack, the real group-navigation model and the real imnodes id mapping.

add_executable(test-flowview
	test_undo.cpp
	test_groupnav.cpp
	test_canvasids.cpp
	test_graphio.cpp
	test_mapscene.cpp
	test_loopscene.cpp
	test_pinkey.cpp
	test_framepattern.cpp
	test_sweep.cpp
	test_valueviews.cpp
	test_catalog.cpp
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/undo.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/groupnav.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/canvasids.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/graphio.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/framepattern.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/runmode.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/scene.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/dump.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/clibinders.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/valueviews.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/imagecanvas.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/imageview.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/sequenceview.cpp"
)

target_compile_features(test-flowview PRIVATE cxx_std_17)
target_include_directories(test-flowview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
# lain::flow::serialize: groupnav's layout tree is an EditorTree (the serialize layer's type).
# lain::image + lain::io::data::codecs: graphio names image::Image / ColorRGBf and reads/writes json.
# lain::gui + Vulkan::Loader: the value views draw with ImGui, so they link it (the same recipe
# libs/gui's own tests use). Still driver-free — the POSTER half is what the tests call, and no
# gui::Context is ever constructed; graphio touches the file system, but no device and no window.
target_link_libraries(test-flowview PRIVATE
	lain::data lain::flow lain::flow::serialize lain::flow-example lain::image
	lain::io lain::io::sequence lain::media::serialize lain::io::image lain::io::data::codecs lain::io::image::codecs
	lain::io::video::codecs
	lain::log lain::string lain::task
	lain::gui Vulkan::Loader
	lain::warnings Catch2::Catch2WithMain
)

# catch_discover_tests RUNS this executable at build time to enumerate its cases, so on Windows a
# missing FFmpeg DLL fails the BUILD rather than the test — stage them beside it.
if(LAIN_IO_VIDEO_FFMPEG)
	lain_ffmpeg_stage_runtime(test-flowview)
endif()

catch_discover_tests(test-flowview TEST_PREFIX "(flowview) " ${LAIN_TEST_DL_PATHS})
