# flowview — the lain::flow inspector. A thin lain::app client over lain::gui. Links
# lain::flow-example for the smoke scene and archimedes for the GPU texture readback the
# cli-mode dump performs. Vulkan::Loader resolves archimedes' vk* symbols for the
# executable. gui-mode (default) opens a window with the inspector; cli-mode (--headless)
# needs no window and dumps the graph.
add_executable(flowview
	src/main.cpp
	src/flowviewapp.cpp
	src/mainwindow.cpp
	src/appcontext.cpp
	src/groupnav.cpp
	src/groupedit.cpp
	src/session.cpp
	src/undo.cpp
	src/previewcache.cpp
	src/panes/menubar.cpp
	src/panes/issuespane.cpp
	src/panes/previewpane.cpp
	src/panes/interfacepane.cpp
	src/panes/inspectorpane.cpp
	src/panes/graphpane.cpp
	src/panes/imagesave.cpp
	src/panes/canvasstate.cpp
	src/canvasids.cpp
	src/parameditors.cpp
	src/scene.cpp
	src/graphio.cpp
	src/runmode.cpp
	src/clibinders.cpp
	src/canvasstyle.cpp
	src/dump.cpp
)

target_compile_features(flowview PRIVATE cxx_std_17)
target_link_libraries(flowview
	PRIVATE lain::app lain::core lain::gui lain::flow-example lain::io lain::io::image lain::io::image::codecs
			lain::flow::serialize lain::io::data::codecs # graph <-> data::Value <-> json
			lain::string lain::log archimedes Vulkan::Loader lain::warnings
)

# Stage the vendored MoltenVK ICD next to the binary (macOS; no-op elsewhere) so
# acm::useStagedVulkanRuntime() finds it and acm::Instance creation succeeds without a
# system Vulkan install or a hand-set VK_ICD_FILENAMES. Without this the loader finds no
# driver, the instance is invalid, and gui-mode trips GLFW's null-instance assert.
acm_stage_vulkan_runtime(flowview)

# Driver-free unit tests for the app's testable internals (the undo history).
if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
