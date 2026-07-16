# Builds the `imgui` static library from Dear ImGui — core + the GLFW and Vulkan
# backends. ImGui ships no CMake of its own, so we define the target. The pinned
# release is fetched via FetchContent (cached under .cache/fetch/ like the other
# deps, extracted to build/_deps). Headers are exposed SYSTEM so a consumer's strict
# flags don't fire on them, and imgui itself is not built with lain's warnings.
#
# The lain-specific config (the IMGUI_USER_CONFIG bridge to lain::math) is applied to
# this target by libs/gui, which owns that glue — not here.

include(FetchContent)

# The DOCKING branch (superset of the release: adds DockSpace / DockBuilder + multi-viewport; lain
# uses docking, multi-viewport is deferred). Docking isn't in the tagged releases, so it's tracked as
# a branch — pinned by commit for reproducibility (like the submodules), not the moving branch tip.
set(IMGUI_REF "docking" CACHE STRING "Dear ImGui docking-branch commit or ref (pin a commit for repro)")
set(IMGUI_FILE "github.com/ocornut/imgui/archive/${IMGUI_REF}.tar.gz")

FetchContent_Declare(imgui
	URL          "https://${IMGUI_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${IMGUI_FILE}"
)
FetchContent_MakeAvailable(imgui) # no CMakeLists.txt — just populates the sources

add_library(imgui STATIC
	"${imgui_SOURCE_DIR}/imgui.cpp"
	"${imgui_SOURCE_DIR}/imgui_draw.cpp"
	"${imgui_SOURCE_DIR}/imgui_tables.cpp"
	"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
add_library(imgui::imgui ALIAS imgui)

target_compile_features(imgui PUBLIC cxx_std_17)
target_include_directories(imgui SYSTEM PUBLIC
	"${imgui_SOURCE_DIR}"
	"${imgui_SOURCE_DIR}/backends"
)
# The backends compile against the Vulkan headers + GLFW; the loader is the linking
# executable's concern (like archimedes, imgui leaves vk* unresolved).
target_link_libraries(imgui PUBLIC Vulkan::Headers glfw)
