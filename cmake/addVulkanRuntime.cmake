# Builds Vulkan-Loader from source against Vulkan::Headers, exposing the
# Vulkan::Loader target. A runnable target that creates a VkInstance (the [gpu]
# tests, and later flowview) needs it: archimedes is a static lib that leaves its
# vk* symbols unresolved, so the executable must link the loader. No system SDK is
# required — the built loader finds the platform's installed ICD (GPU driver) at
# runtime, so [gpu] tests run on a machine with a driver and SKIP without one.
#
# Vulkan::Headers comes from archimedes' addVulkan (pinned to the same SDK series
# as the loader version below); include this only after add_subdirectory(archimedes).

if(NOT TARGET Vulkan::Headers)
	message(FATAL_ERROR "addVulkanRuntime requires Vulkan::Headers (archimedes' addVulkan provides it).")
endif()

if(TARGET Vulkan::Loader)
	return()
endif()

include(FetchContent)

# Match the Vulkan-Headers SDK series archimedes vendors (VULKAN_HEADERS_VER).
set(VULKAN_LOADER_VER "1.4.341.0" CACHE STRING "Vendored Vulkan-Loader SDK version")
set(VULKAN_LOADER_FILE "github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/vulkan-sdk-${VULKAN_LOADER_VER}.tar.gz")

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_WERROR OFF CACHE BOOL "" FORCE)
set(UPDATE_DEPS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(VulkanLoader
	URL          "https://${VULKAN_LOADER_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${VULKAN_LOADER_FILE}"
)
FetchContent_MakeAvailable(VulkanLoader)

# On Apple the loader also builds vulkan-framework (a second full loader compile);
# we only link the plain loader, so drop it from the default build.
if(TARGET vulkan-framework)
	set_target_properties(vulkan-framework PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
