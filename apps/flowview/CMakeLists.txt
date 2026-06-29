# flowview — the lain::flow inspector. A thin lain::app client over lain::gui (panels
# land next step). Links lain::flow-example for the smoke scene and archimedes for the
# GPU texture readback the cli-mode dump performs. Vulkan::Loader resolves
# archimedes' vk* symbols for the executable. cli-mode (--headless) needs no window;
# gui-mode opens one.

add_executable(flowview
	src/main.cpp
	src/flowviewapp.cpp
	src/inspectorwindow.cpp
	src/scene.cpp
	src/dump.cpp
)

target_compile_features(flowview PRIVATE cxx_std_17)
target_link_libraries(flowview
	PRIVATE lain::app lain::gui lain::flow-example archimedes Vulkan::Loader lain::warnings
)
