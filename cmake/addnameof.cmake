# Provides the nameof::nameof target — static reflection for type/variable names
# behind lain::meta (typeName).
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent. The
# download path mirrors the source URL under .cache/fetch/ so similarly named archives
# across deps can't collide; sources extract into build/_deps. Header-only, so this
# contributes an INTERFACE target only.

include(FetchContent)

set(NAMEOF_VER "0.10.5")
set(NAMEOF_FILE "github.com/Neargye/nameof/archive/v${NAMEOF_VER}.tar.gz")

set(NAMEOF_OPT_BUILD_EXAMPLES OFF CACHE BOOL "Disable nameof examples" FORCE)
set(NAMEOF_OPT_BUILD_TESTS OFF CACHE BOOL "Disable nameof tests" FORCE)
set(NAMEOF_OPT_INSTALL OFF CACHE BOOL "Disable nameof install" FORCE)

FetchContent_Declare(nameof
	URL          "https://${NAMEOF_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${NAMEOF_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(nameof)

# Re-expose nameof's headers as SYSTEM so a consumer's strict flags (/WX /W4,
# -Werror -Wall -Wextra) don't fire on the third-party header. Only for the vendored
# build (nameof_SOURCE_DIR is empty when satisfied by a system package).
if(nameof_SOURCE_DIR AND TARGET nameof)
	get_target_property(_nameof_inc nameof INTERFACE_INCLUDE_DIRECTORIES)
	if(_nameof_inc)
		set_target_properties(nameof PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(nameof SYSTEM INTERFACE ${_nameof_inc})
	endif()
endif()
