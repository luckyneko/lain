# Provides the magic_enum::magic_enum target — static reflection for enums (names,
# values, name<->value casts) behind lain::meta.
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent. The
# download path mirrors the source URL under .cache/fetch/ so similarly named archives
# across deps can't collide; sources extract into build/_deps. Header-only, so this
# contributes an INTERFACE target only.

include(FetchContent)

set(MAGICENUM_VER "0.9.8")
set(MAGICENUM_FILE "github.com/Neargye/magic_enum/archive/v${MAGICENUM_VER}.tar.gz")

set(MAGIC_ENUM_OPT_BUILD_EXAMPLES OFF CACHE BOOL "Disable magic_enum examples" FORCE)
set(MAGIC_ENUM_OPT_BUILD_TESTS OFF CACHE BOOL "Disable magic_enum tests" FORCE)
set(MAGIC_ENUM_OPT_INSTALL OFF CACHE BOOL "Disable magic_enum install" FORCE)

FetchContent_Declare(magic_enum
	URL          "https://${MAGICENUM_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${MAGICENUM_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(magic_enum)

# Re-expose magic_enum's headers as SYSTEM so a consumer's strict flags (/WX /W4,
# -Werror -Wall -Wextra) don't fire on the third-party header. Only for the vendored
# build (magic_enum_SOURCE_DIR is empty when satisfied by a system package).
if(magic_enum_SOURCE_DIR AND TARGET magic_enum)
	get_target_property(_me_inc magic_enum INTERFACE_INCLUDE_DIRECTORIES)
	if(_me_inc)
		set_target_properties(magic_enum PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(magic_enum SYSTEM INTERFACE ${_me_inc})
	endif()
endif()
