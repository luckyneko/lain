# Provides the glfw target (window + input + Vulkan surface) for lain::app.
#
# lain owns its GLFW independently of archimedes: archimedes vendors GLFW only for
# its testbed, and that module isn't on lain's module path (and GLFW is windowing,
# not part of archimedes' passed-through Vulkan surface). Prefers a system package;
# otherwise fetches a pinned release via FetchContent (cached under .cache/fetch/).

include(FetchContent)

set(GLFW_VER "3.4" CACHE STRING "Vendored GLFW version")
set(GLFW_FILE "github.com/glfw/glfw/archive/refs/tags/${GLFW_VER}.tar.gz")

# Window/Vulkan-surface only — skip everything else GLFW can build.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw3
	URL          "https://${GLFW_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${GLFW_FILE}"
	FIND_PACKAGE_ARGS NAMES glfw3
)
FetchContent_MakeAvailable(glfw3)

# glfw3.h includes the system's OpenGL header, GL/gl.h, unless GLFW_INCLUDE_NONE is defined. The
# GLFW_INCLUDE_VULKAN that lain's own GLFW files define adds Vulkan's header without stopping that. lain
# draws with Vulkan only, so every consumer of the glfw target gets GLFW_INCLUDE_NONE, ImGui's GLFW
# backend included. Without it a Linux build needs OpenGL's development headers for nothing: CI found
# that when its package list stopped bringing them in through xorg-dev (2026-11-19).
target_compile_definitions(glfw INTERFACE GLFW_INCLUDE_NONE)
