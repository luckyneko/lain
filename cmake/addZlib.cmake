# Provides the ZLIB::ZLIB target (static zlib) — a transitive dependency of the libpng
# and libtiff codec plugins, never linked by lain code directly. Fetches a pinned
# release via FetchContent; install rules are skipped (build-tree use only) and headers
# are re-exposed SYSTEM so a consumer's strict flags don't fire on zlib.
#
# Included by addLibPNG.cmake (and later addLibTIFF.cmake); guarded so a second include
# is a no-op.

if(TARGET ZLIB::ZLIB)
	return()
endif()

include(FetchContent)

set(ZLIB_VER "1.3.1")
set(ZLIB_FILE "github.com/madler/zlib/archive/v${ZLIB_VER}.tar.gz")

set(SKIP_INSTALL_ALL ON CACHE BOOL "zlib: no install (build-tree only)" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "zlib: no example programs" FORCE)

FetchContent_Declare(zlib
	URL          "https://${ZLIB_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${ZLIB_FILE}"
)
FetchContent_MakeAvailable(zlib)

# zlib unconditionally defines a shared `zlib` target alongside `zlibstatic` (no option
# to skip it); we link only the static one, so keep the shared library out of the
# default build.
if(TARGET zlib)
	set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# zlib's static target is `zlibstatic`; normalize to the canonical ZLIB::ZLIB name and
# expose its headers (zlib.h in the source, generated zconf.h in the build) as SYSTEM.
target_include_directories(zlibstatic SYSTEM PUBLIC "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
if(NOT TARGET ZLIB::ZLIB)
	add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
