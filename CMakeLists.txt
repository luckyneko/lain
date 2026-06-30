cmake_minimum_required(VERSION 3.24) # FetchContent FIND_PACKAGE_ARGS

# Detect top-level vs. bundled-as-a-subproject before our own project() call.
# Use a lain-specific name, NOT the bare `NOT_SUBPROJECT` the siblings (and
# Catch2) use internally: that variable leaks into add_subdirectory child scopes,
# and setting it here would make every bundled sibling think *it* is top-level
# and switch its tests/testbed/benchmarks on.
if(NOT DEFINED PROJECT_NAME)
	set(LAIN_NOT_SUBPROJECT ON)
endif()

project(lain LANGUAGES CXX VERSION 0.1.0)

# Default build release.
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

# Disable building in-source.
if(CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

# Tests and apps only when lain is the top-level project (consumed via
# add_subdirectory they stay off, matching the siblings).
option(LAIN_BUILD_TESTING "Build the unit/integration tests" ${LAIN_NOT_SUBPROJECT})
option(LAIN_BUILD_APPS "Build the runnable apps (flowview)" ${LAIN_NOT_SUBPROJECT})
option(LAIN_FORMAT "Provide clang-format format / format-check targets" ${LAIN_NOT_SUBPROJECT})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Shared strict-warning flags for lain's own targets (link PRIVATE; never on a
# vendored dep). Defines the lain::warnings INTERFACE target.
include(lainWarnings)

# clang-format `format` / `format-check` targets, over a vendored pinned binary. The
# set of files is declared in .clang-format-include at the repo root.
if(LAIN_FORMAT)
	include(addclangformat)
endif()

# ---- Vendored / owned dependencies ------------------------------------------
# Owned lain libs are git submodules under extern/, tracking their develop branch
# (no tagged releases yet), added via add_subdirectory. Their testbed/testing
# stays OFF here since lain, not they, is the top-level project.
add_subdirectory(extern/archimedes)     # acm:: Vulkan renderer (for flow + flowview)
# add_subdirectory(extern/thorax)       # deferred — no M1 consumer yet

# Third-party deps via cmake/addXXX.cmake FetchContent modules (prefer system
# package, else pinned release; headers re-exposed SYSTEM under strict flags).
include(addTaskflow)
include(addGLM)
include(addfmt)        # {fmt} — formatting for lain::log (and spdlog's external fmt)
include(addspdlog)     # logging backend behind lain::log (built against addfmt's fmt)
include(addmagicenum)  # enum reflection behind lain::meta
include(addnameof)     # type-name reflection behind lain::meta

# Vulkan loader for lain's own VkInstance-creating executables (the [gpu] tests,
# flowview). archimedes owns Vulkan and exposes acm_require_vulkan_runtime()
# (defined by add_subdirectory above); reusing it keeps the loader version-locked
# to archimedes' headers — no second pin to skew. Builds the loader cross-platform
# and pulls MoltenVK on macOS.
if(LAIN_BUILD_TESTING OR LAIN_BUILD_APPS)
	acm_require_vulkan_runtime()
endif()

if(LAIN_BUILD_TESTING)
	enable_testing() # must be at the project root for ctest to find tests
	include(addcatch2)
endif()

# ---- Libraries (lain::<name>) -----------------------------------------------
add_subdirectory(libs/core)             # foundational std-only types (Time, Version)
add_subdirectory(libs/log)              # spdlog-backed logging (lain::log)
add_subdirectory(libs/string)           # fmt-backed format() + lain-type formatters
add_subdirectory(libs/meta)             # enum reflection (magic_enum) — lain::meta
add_subdirectory(libs/math)             # typed GLM wrapper
add_subdirectory(libs/task)
add_subdirectory(libs/flow)             # the node-graph engine (WORK.md M1)

# ---- App stack + apps -------------------------------------------------------
# The GUI/windowing libs and their deps (GLFW, CLI11, later ImGui) build only with
# the app stack, so a headless consumer of lain::flow isn't forced to vendor them.
if(LAIN_BUILD_APPS)
	include(addGLFW)
	include(addCLI11)
	include(addImGui)
	include(addImnodes)
	add_subdirectory(libs/app)            # GLFW + CLI11 app harness
	add_subdirectory(libs/gui)            # Dear ImGui + imnodes wrapper
	add_subdirectory(apps/flowview)       # the inspector (step 9)
endif()
