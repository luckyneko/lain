# Provides the TIFF::tiff target (static libtiff) behind the lain::io::image::tiff codec
# plugin. Fetches a pinned release via FetchContent, wired to the fetched zlib. Every
# optional codec (jpeg/lzma/zstd/webp/jbig/lerc/libdeflate) and all tools/tests/docs are
# off — only the static library with zlib/Deflate. Headers re-exposed SYSTEM.
#
# Included by the tiff plugin's CMakeLists (fetched only when that plugin is enabled);
# guarded so a second include is a no-op.

if(TARGET TIFF::tiff)
	return()
endif()

include(addZlib) # ZLIB::ZLIB + zlib_SOURCE_DIR
include(FetchContent)

set(LIBTIFF_VER "4.7.0")
set(LIBTIFF_FILE "gitlab.com/libtiff/libtiff/-/archive/v${LIBTIFF_VER}/libtiff-v${LIBTIFF_VER}.tar.gz")

set(SKIP_INSTALL_ALL ON CACHE BOOL "libtiff: no install (build-tree only)" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "libtiff: static only" FORCE)
set(tiff-tools OFF CACHE BOOL "libtiff: no tools" FORCE)
set(tiff-tests OFF CACHE BOOL "libtiff: no tests" FORCE)
set(tiff-docs OFF CACHE BOOL "libtiff: no docs" FORCE)
set(tiff-contrib OFF CACHE BOOL "libtiff: no contrib" FORCE)
set(tiff-deprecated OFF CACHE BOOL "libtiff: no deprecated api" FORCE)
set(cxx OFF CACHE BOOL "libtiff: no C++ wrapper (libtiffxx) — we link the C lib" FORCE)
# Keep the dependency surface to zlib/Deflate only — no optional codec libs.
set(jpeg OFF CACHE BOOL "" FORCE)
set(old-jpeg OFF CACHE BOOL "" FORCE)
set(jbig OFF CACHE BOOL "" FORCE)
set(lzma OFF CACHE BOOL "" FORCE)
set(zstd OFF CACHE BOOL "" FORCE)
set(webp OFF CACHE BOOL "" FORCE)
set(lerc OFF CACHE BOOL "" FORCE)
set(libdeflate OFF CACHE BOOL "" FORCE)
# Point libtiff's ZLIB search at the fetched zlib (headers come through ZLIB::ZLIB too).
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" CACHE PATH "libtiff: fetched zlib headers" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "libtiff: fetched zlib target" FORCE)

FetchContent_Declare(libtiff
	URL          "https://${LIBTIFF_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${LIBTIFF_FILE}"
)
FetchContent_MakeAvailable(libtiff)

# libtiff's static target is `tiff`; expose its headers (tiffio.h in the source, generated
# tiffconf.h / tif_config.h in the build) as SYSTEM, and normalize to TIFF::tiff.
target_include_directories(tiff SYSTEM PUBLIC
	"${libtiff_SOURCE_DIR}/libtiff" "${libtiff_BINARY_DIR}/libtiff")
if(NOT TARGET TIFF::tiff)
	add_library(TIFF::tiff ALIAS tiff)
endif()
