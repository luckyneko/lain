# Provides the stb::image target — the stb_image decoder + stb_image_write encoder (single
# headers) behind the lain::io::image::jpeg codec plugin. stb has no CMake build; FetchContent
# just populates the source, and the implementations are compiled here in their own TU
# (stb_image_impl.cpp, JPEG only) with warnings OFF, so stb's noisy code never meets lain's
# strict flags. The headers are re-exposed SYSTEM for consumers (declarations only — they don't
# define the impl).
#
# Included by the jpeg plugin's CMakeLists; guarded so a second include is a no-op.

if(TARGET stb::image)
	return()
endif()

include(FetchContent)

# stb is a rolling single-header repo with no releases; pin a commit (stb_image v2.30).
set(STB_SHA "31c1ad37456438565541f4919958214b6e762fb4")
set(STB_FILE "github.com/nothings/stb/archive/${STB_SHA}.tar.gz")

FetchContent_Declare(stb
	URL          "https://${STB_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${STB_FILE}"
)
FetchContent_MakeAvailable(stb) # stb has no CMakeLists — this just populates the source

add_library(stb_image STATIC "${CMAKE_CURRENT_LIST_DIR}/stb_image_impl.cpp")
target_include_directories(stb_image SYSTEM PUBLIC "${stb_SOURCE_DIR}")
# stb's implementation is not warning-clean and is not lain code — silence it (never link
# lain::warnings here).
if(MSVC)
	target_compile_options(stb_image PRIVATE /w)
else()
	target_compile_options(stb_image PRIVATE -w)
endif()

add_library(stb::image ALIAS stb_image)
