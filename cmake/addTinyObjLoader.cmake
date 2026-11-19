# Provides the tinyobjloader target — the OBJ / MTL parser behind the lain::io::mesh::obj plugin. Its own
# CMake build is not used: FetchContent only populates the source (SOURCE_SUBDIR names no CMakeLists), and
# the implementation is compiled here in its own TU (tinyobjloader_impl.cpp) with warnings OFF, so its code
# never meets lain's strict flags, as stb's does not. The header is re-exposed SYSTEM.
#
# Included by the obj plugin's CMakeLists; guarded so a second include is a no-op.

if(TARGET tinyobjloader::tinyobjloader)
	return()
endif()

include(FetchContent)

# The project's GitHub "releases" stop at 2019; its tags carry on, and this is the newest (2024-01-26).
set(TINYOBJLOADER_TAG "v2.0.0rc13")
set(TINYOBJLOADER_FILE "github.com/tinyobjloader/tinyobjloader/archive/refs/tags/${TINYOBJLOADER_TAG}.tar.gz")

FetchContent_Declare(tinyobjloader
	URL           "https://${TINYOBJLOADER_FILE}"
	DOWNLOAD_DIR  "${CMAKE_SOURCE_DIR}/.cache/fetch/${TINYOBJLOADER_FILE}"
	SOURCE_SUBDIR "lain-populate-only" # no CMakeLists there, so MakeAvailable only populates
)
FetchContent_MakeAvailable(tinyobjloader)

add_library(tinyobjloader_impl STATIC "${CMAKE_CURRENT_LIST_DIR}/tinyobjloader_impl.cpp")
target_include_directories(tinyobjloader_impl SYSTEM PUBLIC "${tinyobjloader_SOURCE_DIR}")
target_compile_features(tinyobjloader_impl PUBLIC cxx_std_17)
# Not lain code: silence it (never link lain::warnings here).
if(MSVC)
	target_compile_options(tinyobjloader_impl PRIVATE /w)
else()
	target_compile_options(tinyobjloader_impl PRIVATE -w)
endif()

add_library(tinyobjloader::tinyobjloader ALIAS tinyobjloader_impl)

# tinyobjloader's notice for --licenses (lainNotices.cmake).
include(lainNotices)
lain_register_notice(tinyobjloader NAME "tinyobjloader" VERSION "${TINYOBJLOADER_TAG}" SPDX "MIT"
	SOURCE_DIR "${tinyobjloader_SOURCE_DIR}" FILES LICENSE)
