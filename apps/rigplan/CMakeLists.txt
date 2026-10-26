# rigplan — rig design (WORK.md M15). A thin lain::app client: today its `render` subcommand writes a
# rig plan's synthetic capture headless (lain::rigplan::capture); the gui arrives with slices 8 and 9.
add_executable(rigplan
	src/main.cpp
	src/rigplanapp.cpp
)

target_compile_features(rigplan PRIVATE cxx_std_17)
target_link_libraries(rigplan
	PRIVATE lain::app lain::core lain::rigplan::capture lain::rigplan::serialize
			lain::io lain::io::data lain::io::data::codecs lain::io::image::codecs lain::io::mesh::codecs # the rig plan document, the frames, the meshes a plan names
			lain::camera::backends # the board renderer, where a camera plugin is built
			lain::string lain::log archimedes Vulkan::Loader lain::warnings
)

# The vendored MoltenVK ICD beside the binary (macOS; a no-op elsewhere), and archimedes' CPU device,
# as flowview stages them: a capture needs a device, and a machine with no GPU renders on the CPU one.
acm_stage_vulkan_runtime(rigplan)
# The OpenCV DLLs, for the board renderer. Asked of the plugin TARGET, not the option, as flowview does.
if(TARGET lain::camera::opencv)
	lain_opencv_stage_runtime(rigplan)
endif()
# And the loader itself on Windows, where the executable resolves vulkan-1.dll from its own directory.
if(WIN32)
	add_custom_command(TARGET rigplan POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Vulkan::Loader>" "$<TARGET_FILE_DIR:rigplan>"
		COMMENT "Staging the Vulkan loader beside rigplan")
endif()
