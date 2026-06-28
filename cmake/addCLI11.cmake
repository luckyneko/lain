# Provides the CLI11::CLI11 target (header-only command-line parser) for lain::app.
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent
# (cached under .cache/fetch/). Header-only, so this contributes an INTERFACE
# target only; its headers are re-exposed SYSTEM so strict flags don't fire on them.

include(FetchContent)

set(CLI11_VER "2.4.2" CACHE STRING "Vendored CLI11 version")
set(CLI11_FILE "github.com/CLIUtils/CLI11/archive/refs/tags/v${CLI11_VER}.tar.gz")

set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(CLI11
	URL          "https://${CLI11_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${CLI11_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(CLI11)

# Re-expose CLI11's headers as SYSTEM so a consumer's /WX /W4 (or -Werror) doesn't
# fire on the third-party header. Only for the vendored build.
if(cli11_SOURCE_DIR AND TARGET CLI11)
	get_target_property(_cli_inc CLI11 INTERFACE_INCLUDE_DIRECTORIES)
	if(_cli_inc)
		set_target_properties(CLI11 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(CLI11 SYSTEM INTERFACE ${_cli_inc})
	endif()
endif()
