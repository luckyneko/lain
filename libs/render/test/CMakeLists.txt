# lain::render tests (Catch2). test_view.cpp and test_scene.cpp are pure CPU checks. test_renderer.cpp
# and test_viewport.cpp draw on the first device archimedes ranks, and SKIP where there is none: Metal
# through MoltenVK on macOS, or archimedes' CPU device once lain's pin carries it (WORK.md M15 slice 3d).

add_executable(test-render
	test_view.cpp
	test_scene.cpp
	test_renderer.cpp
	test_viewport.cpp
)

target_compile_features(test-render PRIVATE cxx_std_17)
target_link_libraries(test-render PRIVATE lain::render lain::camera lain::warnings Catch2::Catch2WithMain)

# On macOS the loader finds no driver unless the vendored MoltenVK ICD sits beside the executable.
acm_stage_vulkan_runtime(test-render)

catch_discover_tests(test-render TEST_PREFIX "(render) " ${LAIN_TEST_DL_PATHS})
