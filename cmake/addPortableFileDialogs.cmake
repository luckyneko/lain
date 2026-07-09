# Provides the pfd::pfd target — portable-file-dialogs, a single-header, dependency-free
# native file-dialog library (samhocevar/portable-file-dialogs). Used by lain::gui to open the
# OS "open/save file" panels. Header-only: FetchContent just populates the source and we expose
# the header as an INTERFACE target (SYSTEM, so its code never meets lain's strict flags). It
# shells out to the platform's native dialog — osascript on macOS, zenity/kdialog on Linux, the
# Win32 common dialogs on Windows — so it needs no extra link libraries.
#
# Included by the app stack (top-level CMakeLists, LAIN_BUILD_APPS); guarded so a second include
# is a no-op.

if(TARGET pfd::pfd)
	return()
endif()

include(FetchContent)

set(PFD_TAG "0.1.0")
set(PFD_FILE "github.com/samhocevar/portable-file-dialogs/archive/refs/tags/${PFD_TAG}.tar.gz")

# SOURCE_SUBDIR points at a dir with no CMakeLists so MakeAvailable *populates* the source but
# does not add pfd's own CMakeLists (we make our own SYSTEM interface target instead — the
# stb::image treatment, so pfd's header is included SYSTEM and never meets lain's strict flags).
FetchContent_Declare(portablefiledialogs
	URL           "https://${PFD_FILE}"
	DOWNLOAD_DIR  "${CMAKE_SOURCE_DIR}/.cache/fetch/${PFD_FILE}"
	SOURCE_SUBDIR do-not-build
)
FetchContent_MakeAvailable(portablefiledialogs)

add_library(portable_file_dialogs INTERFACE)
target_include_directories(portable_file_dialogs SYSTEM INTERFACE "${portablefiledialogs_SOURCE_DIR}")
add_library(pfd::pfd ALIAS portable_file_dialogs)
