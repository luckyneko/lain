# lain::render tests (Catch2). test_view.cpp, test_scene.cpp and test_raytable.cpp are pure CPU checks.
# test_renderer.cpp, test_distance.cpp, test_viewport.cpp and test_capture.cpp draw on every kind of
# device the machine offers (gpu.h): its hardware GPU, and archimedes' CPU device (SwiftShader) staged
# beside the test.

add_executable(test-render
	test_view.cpp
	test_scene.cpp
	test_renderer.cpp
	test_distance.cpp
	test_viewport.cpp
	test_raytable.cpp
	test_capture.cpp
)

target_compile_features(test-render PRIVATE cxx_std_17)
target_link_libraries(test-render PRIVATE lain::render lain::camera lain::testing lain::warnings Catch2::Catch2WithMain)

# On macOS the loader finds no driver unless the vendored MoltenVK ICD sits beside the executable.
acm_stage_vulkan_runtime(test-render)
# Where the CPU driver is staged beside it, a missing CPU device fails the cases rather than skipping
# them (gpu.h), so a build whose staging broke cannot pass by drawing nothing.
if(ACM_SWIFTSHADER_ICD)
	target_compile_definitions(test-render PRIVATE LAIN_EXPECT_CPU_DEVICE)
endif()

catch_discover_tests(test-render TEST_PREFIX "(render) " ${LAIN_TEST_DL_PATHS})

# What storing a texture costs in memory: its own executable, since it replaces the global aligned operator
# new and delete to count what every image's buffer holds, and a replacement is the whole program's. CPU
# only: nothing here asks for a device.
add_executable(test-render-memory test_storedmemory.cpp)
target_compile_features(test-render-memory PRIVATE cxx_std_17)
target_link_libraries(test-render-memory PRIVATE lain::render lain::image lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-render-memory TEST_PREFIX "(render) " ${LAIN_TEST_DL_PATHS})
