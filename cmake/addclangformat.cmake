# clang-format `format` / `format-check` targets over a pinned, vendored binary so
# every machine + CI formats identically (clang-format output drifts between
# versions — pinning is what makes format-check a meaningful gate). Mirrors the
# addXXX.cmake FetchContent pattern: the binary is cached under .cache/fetch/ like
# the libraries and placed in build/_deps (blow away build/ to clean it). The
# muttleyxd assets are bare executables, not archives, so DOWNLOAD_NO_EXTRACT.
#
# Neither target is part of ALL — invoke explicitly:
#   cmake --build build --target format         # rewrite sources in place
#   cmake --build build --target format-check   # dry-run, non-zero on diff (CI)
#
# The set of files is the single source of truth in .clang-format-include at the repo
# root. Override the binary with -DLAIN_CLANG_FORMAT=/path/to/clang-format.

set(LAIN_CLANG_FORMAT_VERSION "20" CACHE STRING "Pinned clang-format major version")
set(_cf_tag "master-796e77c") # immutable release tag (muttleyxd/clang-tools-static-binaries)

if(CMAKE_HOST_WIN32)
	set(_cf_asset "clang-format-${LAIN_CLANG_FORMAT_VERSION}_windows-amd64.exe")
elseif(CMAKE_HOST_APPLE)
	if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		set(_cf_asset "clang-format-${LAIN_CLANG_FORMAT_VERSION}_macos-arm-arm64")
	else()
		set(_cf_asset "clang-format-${LAIN_CLANG_FORMAT_VERSION}_macosx-amd64")
	endif()
else()
	set(_cf_asset "clang-format-${LAIN_CLANG_FORMAT_VERSION}_linux-amd64")
endif()

if(LAIN_CLANG_FORMAT)
	# Caller supplied their own binary — trust it.
	set(_cf_bin "${LAIN_CLANG_FORMAT}")
else()
	include(FetchContent)
	set(_cf_file "github.com/muttleyxd/clang-tools-static-binaries/releases/download/${_cf_tag}/${_cf_asset}")
	FetchContent_Declare(clangformat
		URL          "https://${_cf_file}"
		DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${_cf_file}"
		DOWNLOAD_NO_EXTRACT TRUE
	)
	FetchContent_MakeAvailable(clangformat)
	set(_cf_bin "${clangformat_SOURCE_DIR}/${_cf_asset}")
	if(NOT CMAKE_HOST_WIN32)
		file(CHMOD "${_cf_bin}" PERMISSIONS
			OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
	endif()
endif()

# Resolve .clang-format-include -> the file list. Each non-comment line is either a
# folder (globbed recursively for C/C++ sources) or an explicit glob (contains '*').
file(STRINGS "${CMAKE_SOURCE_DIR}/.clang-format-include" _cf_lines)
set(_cf_files "")
foreach(_line IN LISTS _cf_lines)
	string(STRIP "${_line}" _line)
	if(_line STREQUAL "" OR _line MATCHES "^#")
		continue()
	endif()
	if(_line MATCHES "[*]")
		file(GLOB_RECURSE _cf_matched CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${_line}")
	else()
		file(GLOB_RECURSE _cf_matched CONFIGURE_DEPENDS
			"${CMAKE_SOURCE_DIR}/${_line}/*.h"
			"${CMAKE_SOURCE_DIR}/${_line}/*.inl"
			"${CMAKE_SOURCE_DIR}/${_line}/*.cpp")
	endif()
	list(APPEND _cf_files ${_cf_matched})
endforeach()
list(REMOVE_DUPLICATES _cf_files)

add_custom_target(format
	COMMAND "${_cf_bin}" -i --style=file ${_cf_files}
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Formatting sources (clang-format ${LAIN_CLANG_FORMAT_VERSION}, .clang-format-include)"
	VERBATIM)

add_custom_target(format-check
	COMMAND "${_cf_bin}" --dry-run --Werror --style=file ${_cf_files}
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Checking formatting (clang-format ${LAIN_CLANG_FORMAT_VERSION}, .clang-format-include)"
	VERBATIM)
