# lain::gui — the inspector toolkit: Dear ImGui (re-exposed as lain::gui) + its
# GLFW/Vulkan backends, bridged to lain::math, integrated over a lain::app window +
# archimedes handles. (imnodes — the node canvas — is added with the canvas; it lags
# ImGui's internals, so it's pinned against ImGui then. See WORK.md.)
#
# The lain config for ImGui lives here: apply the IMGUI_USER_CONFIG bridge header +
# the lain::math link to the imgui target (created by addImGui) — PUBLIC, so imgui's
# own TUs and every consumer share one ImVec definition (ODR).
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="lain/gui/imconfig_lain.h")
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(imgui PUBLIC lain::math)

add_library(gui STATIC
	src/context.cpp
)
add_library(lain::gui ALIAS gui)

target_compile_features(gui PUBLIC cxx_std_17)
target_include_directories(gui PUBLIC include)
target_link_libraries(gui
	PUBLIC imgui lain::app lain::math archimedes
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
