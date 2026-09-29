# lain::gui tests (Catch2). enable_testing() + addcatch2 are at the project root.
# test_gui.cpp is pure compile/behaviour checks: ImGui is re-exposed under lain::gui, and the
# ImVec <-> lain::math bridge converts both ways — no window / driver needed. test_texture.cpp
# drives a real gui::Context on the live driver, opt-in behind LAIN_GUI_SMOKE=1 like lain::app's
# windowed cases. Vulkan::Loader resolves any vk* pulled from imgui's vulkan backend at link time.

add_executable(test-gui
	test_gui.cpp
	test_texture.cpp
)

target_compile_features(test-gui PRIVATE cxx_std_17)
# The windowed gate reads an env var via std::getenv, which MSVC flags C4996 under /WX.
target_compile_definitions(test-gui PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
target_link_libraries(test-gui PRIVATE lain::gui Vulkan::Loader lain::warnings Catch2::Catch2WithMain)

# The windowed cases need a driver, and on macOS the loader finds none unless the vendored MoltenVK
# ICD sits beside the executable. No-op elsewhere.
acm_stage_vulkan_runtime(test-gui)

catch_discover_tests(test-gui TEST_PREFIX "(gui) " ${LAIN_TEST_DL_PATHS})
