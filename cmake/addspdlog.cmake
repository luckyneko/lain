# Provides the spdlog::spdlog target — the logging backend behind lain::log (named
# only in libs/log/src/log.cpp, never past the lain::log seam).
#
# Built against the shared fmt::fmt from addfmt (SPDLOG_FMT_EXTERNAL), so spdlog and
# lain::log's public header agree on one fmt. include(addfmt) must run first.
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent. The
# download path mirrors the source URL under .cache/fetch/ so similarly named archives
# across deps can't collide; sources extract into build/_deps.

include(FetchContent)

set(SPDLOG_VER "1.14.1")
set(SPDLOG_FILE "github.com/gabime/spdlog/archive/v${SPDLOG_VER}.tar.gz")

# Use the shared external fmt (addfmt's fmt::fmt) instead of spdlog's bundled copy,
# so the program links exactly one fmt.
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Build spdlog against lain's fmt::fmt" FORCE)

# Static (default) — keeps spdlog out of any shared export table. Don't install or
# test it as part of the lain build.
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable spdlog install" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)

FetchContent_Declare(spdlog
	URL          "https://${SPDLOG_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${SPDLOG_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(spdlog)

# The hardening below applies only to the vendored build (spdlog_SOURCE_DIR is empty
# when the dependency was satisfied by a system package).
if(spdlog_SOURCE_DIR AND TARGET spdlog)
	# Re-expose spdlog's headers as SYSTEM so a consumer's strict flags don't fire on
	# third-party headers (lain::log links spdlog PRIVATE, so this mainly guards log.cpp).
	get_target_property(_spdlog_inc spdlog INTERFACE_INCLUDE_DIRECTORIES)
	if(_spdlog_inc)
		set_target_properties(spdlog PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(spdlog SYSTEM INTERFACE ${_spdlog_inc})
	endif()

	# Keep it PIC + hidden-visibility so its symbols are never re-exported should it
	# ever land in a shared library downstream.
	set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
	if(NOT MSVC)
		target_compile_options(spdlog PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
	endif()
endif()
