# lain::gui tests (Catch2). enable_testing() + addcatch2 are at the project root.
# Pure compile/behaviour checks: ImGui is re-exposed under lain::gui, and the
# ImVec <-> lain::math bridge converts both ways. No window / driver needed (the
# windowed Context seam is exercised by flowview). Vulkan::Loader resolves any vk*
# pulled from imgui's vulkan backend at link time.

add_executable(test-gui
	test_gui.cpp
)

target_compile_features(test-gui PRIVATE cxx_std_17)
target_link_libraries(test-gui PRIVATE lain::gui Vulkan::Loader lain::warnings Catch2::Catch2WithMain)

catch_discover_tests(test-gui TEST_PREFIX "(gui) " ${LAIN_TEST_DL_PATHS})
