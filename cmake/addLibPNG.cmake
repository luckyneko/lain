# Provides the PNG::png target (static libpng) behind the lain::io::image::png codec
# plugin. Fetches a pinned release via FetchContent, wired to the fetched zlib (its
# find_package(ZLIB) is bypassed via PNG_BUILD_ZLIB). Shared lib / tools / tests /
# install are all off — only the static library is built. Headers are re-exposed SYSTEM
# so the plugin's strict flags don't fire on png.h / zlib.h.
#
# Included by the png plugin's CMakeLists (so libpng is fetched only when that plugin is
# enabled); guarded so a second include is a no-op.

if(TARGET PNG::png)
	return()
endif()

include(addZlib) # ZLIB::ZLIB + the zlib_SOURCE_DIR / zlib_BINARY_DIR used below
include(FetchContent)

set(LIBPNG_VER "1.6.44")
set(LIBPNG_FILE "github.com/pnggroup/libpng/archive/v${LIBPNG_VER}.tar.gz")

set(SKIP_INSTALL_ALL ON CACHE BOOL "libpng: no install (build-tree only)" FORCE)
set(PNG_BUILD_ZLIB ON CACHE BOOL "libpng: use the fetched zlib, skip find_package(ZLIB)" FORCE)
set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}" CACHE STRING "libpng: fetched zlib headers" FORCE)
set(ZLIB_LIBRARIES zlibstatic CACHE STRING "libpng: fetched zlib target" FORCE)
set(PNG_SHARED OFF CACHE BOOL "libpng: static only" FORCE)
set(PNG_STATIC ON CACHE BOOL "libpng: build the static library" FORCE)
set(PNG_TESTS OFF CACHE BOOL "libpng: no tests" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "libpng: no tools" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "libpng: no macOS framework" FORCE)

FetchContent_Declare(libpng
	URL          "https://${LIBPNG_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${LIBPNG_FILE}"
)
FetchContent_MakeAvailable(libpng)

# libpng's static target is `png_static`; it links zlib but doesn't export usable
# build-tree include dirs, so add them (png.h in the source, generated pnglibconf.h in
# the build) as SYSTEM, and normalize to the canonical PNG::png name.
target_include_directories(png_static SYSTEM PUBLIC "${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}")
if(NOT TARGET PNG::png)
	add_library(PNG::png ALIAS png_static)
endif()
