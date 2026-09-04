# lain::app tests (Catch2). enable_testing() + addcatch2 are at the project root.
# The [app] tests drive the headless (cli-mode) path through a real flow Graph — no
# driver needed. The opt-in [gpu] windowed smoke (LAIN_GUI_SMOKE=1) opens a window
# on the live driver. Vulkan::Loader resolves archimedes' vk* symbols for the exe.

add_executable(test-app
	test_app.cpp
)

target_compile_features(test-app PRIVATE cxx_std_17)
# The gui-smoke gate reads an env var via std::getenv, which MSVC flags C4996 under
# /WX. Quiet it for this test TU only (the library itself uses no such call off Apple).
target_compile_definitions(test-app PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
target_link_libraries(test-app PRIVATE
	lain::app
	lain::flow
	lain::log
	lain::meta
	Vulkan::Loader
	lain::warnings
	Catch2::Catch2WithMain
)

catch_discover_tests(test-app TEST_PREFIX "(app) " ${LAIN_TEST_DL_PATHS})
