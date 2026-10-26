# lain::rigplan::capture tests (Catch2). The [gpu] cases write a capture on every kind of device the
# machine offers (gpu.h); the refusal and estimate cases need none. Boards are drawn by a stand-in
# renderer (standinboard.h), so the cases are the same in every build.

add_executable(test-rigplan-capture
	test_capture.cpp
)

target_compile_features(test-rigplan-capture PRIVATE cxx_std_17)
# The rig plan the rigplan tests share, the stand-in board renderer, and the device the cases draw on.
target_include_directories(test-rigplan-capture PRIVATE
	"${PROJECT_SOURCE_DIR}/libs/rigplan/test" "${PROJECT_SOURCE_DIR}/libs/rigplan/scene/test" "${PROJECT_SOURCE_DIR}/libs/render/test"
)
target_link_libraries(test-rigplan-capture PRIVATE
	lain::rigplan::capture lain::rigplan::serialize lain::camera::serialize
	lain::io::image lain::io::image::codecs lain::io::data lain::io::data::codecs lain::io::sequence
	lain::string lain::testing lain::warnings Catch2::Catch2WithMain
)

acm_stage_vulkan_runtime(test-rigplan-capture)
# Where the CPU driver is staged beside it, a missing CPU device fails the cases rather than skipping
# them (gpu.h), so a build whose staging broke cannot pass by drawing nothing.
if(ACM_SWIFTSHADER_ICD)
	target_compile_definitions(test-rigplan-capture PRIVATE LAIN_EXPECT_CPU_DEVICE)
endif()

catch_discover_tests(test-rigplan-capture TEST_PREFIX "(rigplan::capture) " ${LAIN_TEST_DL_PATHS})
