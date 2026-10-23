# lain::app tests (Catch2). enable_testing() + addcatch2 are at the project root.
# The [app] tests drive the headless (cli-mode) path through a real flow Graph — no
# driver needed. The opt-in [gpu] windowed cases (LAIN_GUI_SMOKE=1) open a window
# on the live driver. Vulkan::Loader resolves archimedes' vk* symbols for the exe.

add_executable(test-app
	test_app.cpp
	test_notices.cpp
)

target_compile_features(test-app PRIVATE cxx_std_17)
# The gui-smoke gate reads an env var via std::getenv, which MSVC flags C4996 under
# /WX. Quiet it for this test TU only (the library itself uses no such call off Apple).
target_compile_definitions(test-app PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

# Which optional dependencies this build contains, so test_notices.cpp can hold --licenses to the
# configuration both ways: a notice for each one built, none for one that was not. Asked of the plugin
# that brings each in, as flowview's catalog test asks, since the option alone says nothing on a
# platform with no prebuilt; MoltenVK of the dylib archimedes vendors. lain's own LICENSE is read by the
# test, to check a notice is the file's text and not a stub.
function(_lain_expect_built definition)
	if(${ARGN})
		target_compile_definitions(test-app PRIVATE ${definition}=1)
	else()
		target_compile_definitions(test-app PRIVATE ${definition}=0)
	endif()
endfunction()
_lain_expect_built(LAIN_BUILT_MOLTENVK DEFINED ACM_MOLTENVK_DYLIB)
_lain_expect_built(LAIN_BUILT_JSON TARGET lain::io::data::json)
_lain_expect_built(LAIN_BUILT_JPEG TARGET lain::io::image::jpeg)
_lain_expect_built(LAIN_BUILT_PNG TARGET lain::io::image::png)
_lain_expect_built(LAIN_BUILT_TIFF TARGET lain::io::image::tiff)
_lain_expect_built(LAIN_BUILT_OBJ TARGET lain::io::mesh::obj)
_lain_expect_built(LAIN_BUILT_FFMPEG TARGET lain::io::video::ffmpeg)
_lain_expect_built(LAIN_BUILT_OPENCV TARGET lain::camera::opencv)
_lain_expect_built(LAIN_BUILT_CERES TARGET lain::camera::ceres)
target_compile_definitions(test-app PRIVATE LAIN_LICENSE_FILE="${lain_SOURCE_DIR}/LICENSE")
target_link_libraries(test-app PRIVATE
	lain::app
	lain::flow
	lain::log
	lain::meta
	lain::task # the pool-lifetime case asks task::threadCount() from inside onProcess
	Vulkan::Loader
	lain::warnings
	Catch2::Catch2WithMain
)

# The [gpu] cases open a real window, and on macOS the loader finds no driver unless the vendored
# MoltenVK ICD sits beside the executable — without it they fail on the instance before a window
# exists, however LAIN_GUI_SMOKE is set. No-op elsewhere.
acm_stage_vulkan_runtime(test-app)

catch_discover_tests(test-app TEST_PREFIX "(app) " ${LAIN_TEST_DL_PATHS})
