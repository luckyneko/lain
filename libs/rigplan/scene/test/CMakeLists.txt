# lain::rigplan::scene tests (Catch2). test_scene.cpp checks the scene on the CPU, writing a picture
# and a mesh clip to scratch paths where an asset must load; test_scenerender.cpp draws one on the first
# device archimedes ranks, and SKIPs where there is none.

add_executable(test-rigplan-scene
	test_scene.cpp
	test_sceneassets.cpp
	test_scenerender.cpp
)

target_compile_features(test-rigplan-scene PRIVATE cxx_std_17)
# The rig plan the rigplan tests share, and the device the render tests draw on.
target_include_directories(test-rigplan-scene PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test" "${PROJECT_SOURCE_DIR}/libs/render/test")
target_link_libraries(test-rigplan-scene PRIVATE
	lain::rigplan::scene
	lain::io::image lain::io::image::codecs lain::io::mesh lain::io::mesh::codecs
	lain::testing lain::warnings Catch2::Catch2WithMain
)

acm_stage_vulkan_runtime(test-rigplan-scene)
# Where the CPU driver is staged beside it, a missing CPU device fails the cases rather than skipping
# them (gpu.h), so a build whose staging broke cannot pass by drawing nothing.
if(ACM_SWIFTSHADER_ICD)
	target_compile_definitions(test-rigplan-scene PRIVATE LAIN_EXPECT_CPU_DEVICE)
endif()

catch_discover_tests(test-rigplan-scene TEST_PREFIX "(rigplan::scene) " ${LAIN_TEST_DL_PATHS})

# A board mover, drawn by a stand-in board renderer: an executable of its own, since a renderer
# registry only grows and test-rigplan-scene's must stay empty (a board with no renderer is named).
add_executable(test-rigplan-scene-board
	test_sceneboard.cpp
)
target_compile_features(test-rigplan-scene-board PRIVATE cxx_std_17)
target_include_directories(test-rigplan-scene-board PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test" "${PROJECT_SOURCE_DIR}/libs/render/test")
target_link_libraries(test-rigplan-scene-board PRIVATE lain::rigplan::scene lain::testing lain::warnings Catch2::Catch2WithMain)
acm_stage_vulkan_runtime(test-rigplan-scene-board)
if(ACM_SWIFTSHADER_ICD)
	target_compile_definitions(test-rigplan-scene-board PRIVATE LAIN_EXPECT_CPU_DEVICE)
endif()
catch_discover_tests(test-rigplan-scene-board TEST_PREFIX "(rigplan::scene) " ${LAIN_TEST_DL_PATHS})
