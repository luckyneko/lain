cmake_minimum_required(VERSION 3.24) # FetchContent FIND_PACKAGE_ARGS

# Detect top-level vs. bundled-as-a-subproject before our own project() call.
# Use a lain-specific name, NOT the bare `NOT_SUBPROJECT` the siblings (and
# Catch2) use internally: that variable leaks into add_subdirectory child scopes,
# and setting it here would make every bundled sibling think *it* is top-level
# and switch its tests/testbed/benchmarks on.
if(NOT DEFINED PROJECT_NAME)
	set(LAIN_NOT_SUBPROJECT ON)
endif()

# C as well as CXX: the build compiles C throughout (libpng, zlib, libtiff, stb, and the Vulkan
# loader), and the sibling repos declare both. Declaring it here is also this tree's first probe at
# the Windows loader link failure (CI 2026-09-04), where MASM-defined trampolines went unassembled.
project(lain LANGUAGES CXX C VERSION 0.1.0)

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

# Vulkan loader for lain's own VkInstance-creating executables (the [gpu] tests,
# flowview). archimedes owns Vulkan and exposes acm_require_vulkan_runtime()
# (defined by add_subdirectory above); reusing it keeps the loader version-locked
# to archimedes' headers — no second pin to skew. Builds the loader cross-platform
# and pulls MoltenVK on macOS.
if(LAIN_BUILD_TESTING OR LAIN_BUILD_APPS)
	acm_require_vulkan_runtime()

	# Say which assembler owns the loader's .asm, because in this tree two languages claim that
	# extension. The loader enables ASM_MASM for its unknown-function trampolines; libpng then
	# declares `project(libpng LANGUAGES C ASM)` unconditionally, and CMake resolves a source's
	# language by extension across every ENABLED language, so plain ASM — whose Windows
	# "assembler" is cl.exe — took the file instead. Nothing errored: the trampolines were simply
	# never assembled, and the loader failed to link with 750 unresolved vkdev_ext symbols (CI,
	# 2026-09-04). archimedes never sees this because it builds no libpng.
	#
	# Stated rather than arranged: reordering these two so ASM_MASM is enabled last would also
	# work today, but positional fixes to a language-resolution rule nobody can see are how this
	# silently comes back. A no-op everywhere but MSVC, where the collision exists.
	# archimedes' objects call vk* directly, so anything linking archimedes needs the loader AFTER
	# it on the link line. Saying so here is what makes that true everywhere: listing the loader in
	# an executable's own target_link_libraries does not, because CMake emits direct dependencies
	# before transitive ones, and archimedes arrives transitively (through lain::gui or lain::app).
	# In test-flowview that put the loader at position 34 and libarchimedes.a at 109 — fine for
	# Apple's linker, which resolves shared libraries globally, but GNU ld links left to right and
	# Ubuntu defaults to --as-needed, so it dropped the loader before ever seeing the references
	# and the link failed on vkDestroyBuffer (CI, 2026-09-04).
	#
	# archimedes leaves this to consumers by design, since it cannot know whether one wants the
	# vendored loader. By here that choice is made: acm_require_vulkan_runtime() has just run.
	if(TARGET archimedes AND TARGET Vulkan::Loader)
		target_link_libraries(archimedes INTERFACE Vulkan::Loader)
	endif()

	# Catch2 runs each test executable at BUILD time to enumerate its cases, and ctest runs them
	# again afterwards. On Windows the loader is a DLL (vulkan-1.dll) that nothing puts on the
	# path, so any test linking it died with STATUS_DLL_NOT_FOUND in both phases — reported as a
	# test-discovery error, which names neither the DLL nor the loader (CI, 2026-09-04). DL_PATHS
	# covers discovery and the run together; empty and harmless off Windows. archimedes' own test
	# suite hit this first and solves it the same way.
	set(LAIN_TEST_DL_PATHS DL_PATHS "$<TARGET_FILE_DIR:Vulkan::Loader>")

	if(MSVC AND TARGET vulkan)
		get_target_property(_lain_loader_dir vulkan SOURCE_DIR)
		set_source_files_properties("${_lain_loader_dir}/unknown_ext_chain_masm.asm"
			TARGET_DIRECTORY vulkan
			PROPERTIES LANGUAGE ASM_MASM)
	endif()
endif()

if(LAIN_BUILD_TESTING)
	enable_testing() # must be at the project root for ctest to find tests
	include(addcatch2)
endif()

# ---- Libraries (lain::<name>) -----------------------------------------------
add_subdirectory(libs/core)             # foundational std-only types (Time, Version)
add_subdirectory(libs/memory)           # Buffer + aligned alloc seam (std-only, WORK.md M3)
add_subdirectory(libs/log)              # spdlog-backed logging (lain::log)
add_subdirectory(libs/string)           # fmt-backed format() + lain-type formatters
add_subdirectory(libs/meta)             # enum reflection (magic_enum) — lain::meta
add_subdirectory(libs/math)             # typed GLM wrapper
add_subdirectory(libs/image)            # CPU raster + pixel formats (lain::image)
add_subdirectory(libs/media)            # frame sequences: finite lazy footage (WORK.md M10)
add_subdirectory(libs/io)               # transport seam: read(uri) -> Buffer (WORK.md M3)
add_subdirectory(libs/data)             # serialization DOM (Value) + reflection (WORK.md Tier A #1)
add_subdirectory(libs/task)
add_subdirectory(libs/flow)             # the node-graph engine (WORK.md M1)

# Test-support, built only when there are tests to support (see libs/testing/scratch.h).
if(LAIN_BUILD_TESTING)
	add_subdirectory(libs/testing)      # collision-free scratch paths for parallel ctest
endif()

# ---- Plugins (swappable satellites: lain::io::image::<fmt> codecs) ----------
add_subdirectory(plugins)

# ---- App stack + apps -------------------------------------------------------
# The GUI/windowing libs and their deps (GLFW, CLI11, later ImGui) build only with
# the app stack, so a headless consumer of lain::flow isn't forced to vendor them.
if(LAIN_BUILD_APPS)
	include(addGLFW)
	include(addCLI11)
	include(addImGui)
	include(addImnodes)
	include(addPortableFileDialogs)
	add_subdirectory(libs/app)            # GLFW + CLI11 app harness
	add_subdirectory(libs/gui)            # Dear ImGui + imnodes wrapper
	add_subdirectory(apps/flowview)       # the inspector (step 9)
endif()
