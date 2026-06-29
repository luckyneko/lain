# Provides the fmt::fmt target — the {fmt} formatting library. It is both the
# formatting vocabulary lain::log's public header builds on (C++17 has no
# std::format) and the backend spdlog is told to use (SPDLOG_FMT_EXTERNAL in
# addspdlog), so there is one shared fmt rather than spdlog's bundled copy.
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent.
# The download path mirrors the source URL under .cache/fetch/ so similarly named
# archives across deps can't collide; sources extract into build/_deps.

include(FetchContent)

set(FMT_VER "10.2.1")
set(FMT_FILE "github.com/fmtlib/fmt/archive/${FMT_VER}.tar.gz")

set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install" FORCE)
set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)

FetchContent_Declare(fmt
	URL          "https://${FMT_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${FMT_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(fmt)

# Re-expose fmt's headers as SYSTEM so a consumer's strict flags (/WX /W4,
# -Werror -Wall -Wextra) don't fire on the third-party templates. Only for the
# vendored build (fmt_SOURCE_DIR is empty when satisfied by a system package).
if(fmt_SOURCE_DIR AND TARGET fmt)
	get_target_property(_fmt_inc fmt INTERFACE_INCLUDE_DIRECTORIES)
	if(_fmt_inc)
		set_target_properties(fmt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(fmt SYSTEM INTERFACE ${_fmt_inc})
	endif()
endif()
