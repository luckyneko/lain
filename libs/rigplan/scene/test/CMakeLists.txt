# lain::rigplan::scene tests (Catch2). test_scene.cpp checks the scene on the CPU, writing a picture
# and a mesh clip to scratch paths where an asset must load; test_scenerender.cpp draws one on the first
# device archimedes ranks, and SKIPs where there is none.

add_executable(test-rigplan-scene
	test_scene.cpp
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

catch_discover_tests(test-rigplan-scene TEST_PREFIX "(rigplan::scene) " ${LAIN_TEST_DL_PATHS})
