# Provides Catch2::Catch2WithMain + the catch_discover_tests() helper.
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent.
# The download path mirrors the source URL under .cache/fetch/ so similarly
# named archives across deps can't collide; sources extract into build/_deps.

include(FetchContent)

set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 SelfTests")
set(CATCH_ENABLE_WERROR OFF CACHE BOOL "Disable Catch2 Werror")

set(CATCH2_VER "3.14.0")
set(CATCH2_FILE "github.com/catchorg/Catch2/archive/v${CATCH2_VER}.tar.gz")

# A system Catch2 is accepted from 3.9.0, the release that made FAIL and SKIP [[noreturn]]: tests
# write no `return` after a FAIL (MSVC calls one unreachable code, C4702), and before 3.9 the same
# function is one that does not return on every path.
FetchContent_Declare(Catch2
	URL          "https://${CATCH2_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${CATCH2_FILE}"
	FIND_PACKAGE_ARGS 3.9.0 CONFIG
)
FetchContent_MakeAvailable(Catch2)

# Catch2 only propagates its extras/ dir (where Catch.cmake lives) to the parent
# scope `if(NOT NOT_SUBPROJECT)` — and `lain` (like the siblings) sets
# NOT_SUBPROJECT ON, which leaks into Catch2's subdirectory scope and suppresses
# that propagation. Put the helper dir on the path ourselves so include(Catch)
# resolves regardless of scope nesting. catch2_SOURCE_DIR is empty when a system
# package satisfied the dependency (its config supplies Catch.cmake instead).
if(catch2_SOURCE_DIR)
	list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
