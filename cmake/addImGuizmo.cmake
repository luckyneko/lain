# Builds the `imguizmo` static library (CedricGuillemet/ImGuizmo): the 3D manipulation gizmo
# (translate / rotate handles drawn over a pane), used by rigplan's Viewport (WORK.md M15 slice 8).
# Like imnodes, we define the target ourselves so it is built against our imgui target, and therefore
# the same IMGUI_USER_CONFIG / ImVec bridge: one definition across the program. Its CMakeLists builds
# the whole collection (a graph editor, a sequencer, a curve editor ...) and would fetch an ImGui of
# its own for the example, so SOURCE_SUBDIR points at a nonexistent dir and only ImGuizmo.cpp is built.
#
# Pinned by commit, never a moving branch, as imnodes is. This commit is checked against IMGUI_REF
# (addImGui.cmake, 1.92.9 WIP, IMGUI_VERSION_NUM 19285): since ImGui 1.92.8 swapped the last two
# arguments of AddRect / AddPolyline, ImGuizmo calls the new signature (#414, #418), and its version
# guards are `IMGUI_VERSION_NUM < 19276`. A bump of either wants the other looked at.

include(FetchContent)

set(IMGUIZMO_REF "105e4fde45e4e74aae8973d98750820ef7aca7e2" CACHE STRING "Vendored ImGuizmo commit")
set(IMGUIZMO_FILE "github.com/CedricGuillemet/ImGuizmo/archive/${IMGUIZMO_REF}.tar.gz")

FetchContent_Declare(imguizmo
	URL           "https://${IMGUIZMO_FILE}"
	DOWNLOAD_DIR  "${CMAKE_SOURCE_DIR}/.cache/fetch/${IMGUIZMO_FILE}"
	SOURCE_SUBDIR "skip-imguizmo-cmakelists" # nonexistent -> populate only, we define the target
)
FetchContent_MakeAvailable(imguizmo)

add_library(imguizmo STATIC
	"${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp"
)
add_library(imguizmo::imguizmo ALIAS imguizmo)

target_compile_features(imguizmo PUBLIC cxx_std_17)
target_include_directories(imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
# ImGuizmo is ImGui code: it needs the imgui headers (imgui_internal.h included) and our
# IMGUI_USER_CONFIG, both of which ride in on imgui's PUBLIC usage requirements (the config is applied
# by libs/gui). Not built with lain's warnings, as imgui and imnodes are not.
target_link_libraries(imguizmo PUBLIC imgui)

# ImGuizmo's notice for --licenses (lainNotices.cmake).
include(lainNotices)
lain_register_notice(imguizmo NAME "ImGuizmo" VERSION "${IMGUIZMO_REF}" SPDX "MIT"
	SOURCE_DIR "${imguizmo_SOURCE_DIR}" FILES LICENSE)
