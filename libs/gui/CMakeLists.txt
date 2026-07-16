# lain::gui — the inspector toolkit: Dear ImGui (re-exposed as lain::gui) + its
# GLFW/Vulkan backends + imnodes (the node canvas, re-exposed as lain::gui::nodes),
# bridged to lain::math, integrated over a lain::app window + archimedes handles.
#
# The lain config for ImGui lives here: apply the IMGUI_USER_CONFIG bridge header +
# the lain::math link to the imgui target (created by addImGui) — PUBLIC, so imgui's
# own TUs, imnodes, and every consumer share one ImVec definition (ODR).
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="lain/gui/imconfig_lain.h")
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(imgui PUBLIC lain::math)

add_library(gui STATIC
	src/context.cpp
	src/dialogs.cpp
	src/dock.cpp
	src/texture.cpp
)
add_library(lain::gui ALIAS gui)

target_compile_features(gui PUBLIC cxx_std_17)
target_include_directories(gui PUBLIC include)
target_link_libraries(gui
	# lain::image is PUBLIC: gui's headers speak image types (texture.h uploads an image::Image,
	# color.h packs an image::ColorRGBA8), so a consumer needs image on its include path.
	PUBLIC imgui imnodes lain::app lain::math lain::meta lain::image archimedes
	PRIVATE lain::warnings pfd::pfd
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
