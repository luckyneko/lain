# Provides nlohmann_json::nlohmann_json — the JSON parser/serializer, the private backend of the
# lain::io::data::json codec plugin. Never named by libs/io/data core (which stays parser-free);
# fetched only because that plugin is enabled. Header-only.
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent. Guarded so a
# second include is a no-op.

if(TARGET nlohmann_json::nlohmann_json)
	return()
endif()

include(FetchContent)

set(NLOHMANN_VER "3.11.3")
set(NLOHMANN_FILE "github.com/nlohmann/json/archive/v${NLOHMANN_VER}.tar.gz")

set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann/json tests" FORCE)
set(JSON_Install OFF CACHE BOOL "Disable nlohmann/json install" FORCE)

FetchContent_Declare(nlohmann_json
	URL          "https://${NLOHMANN_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${NLOHMANN_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(nlohmann_json)

# Re-expose the headers as SYSTEM so a consumer's strict flags (/WX /W4, -Werror -Wall -Wextra)
# don't fire on the third-party templates. Only for the vendored build (nlohmann_json_SOURCE_DIR is
# empty when satisfied by a system package).
if(nlohmann_json_SOURCE_DIR AND TARGET nlohmann_json)
	get_target_property(_nj_inc nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
	if(_nj_inc)
		set_target_properties(nlohmann_json PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(nlohmann_json SYSTEM INTERFACE ${_nj_inc})
	endif()
endif()
