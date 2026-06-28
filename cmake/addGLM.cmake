# Provides the glm::glm target (the GLM header-only math library behind lain::math).
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent.
# The download path mirrors the source URL under .cache/fetch/ so similarly named
# archives across deps can't collide; sources extract into build/_deps. GLM is
# header-only, so this contributes an INTERFACE target only.

include(FetchContent)

set(GLM_VER "1.0.3")
set(GLM_FILE "github.com/g-truc/glm/archive/${GLM_VER}.tar.gz")

# Header-only: keep GLM's optional compiled library / tests / install out of our
# build graph.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Keep GLM header-only" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "Disable GLM tests" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Disable GLM install" FORCE)

FetchContent_Declare(glm
	URL          "https://${GLM_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${GLM_FILE}"
	FIND_PACKAGE_ARGS NAMES glm CONFIG
)
FetchContent_MakeAvailable(glm)

# GLM exports a `glm::glm` alias from its own CMake; normalize defensively so
# lain::math can always link `glm::glm` regardless of how it was satisfied.
if(TARGET glm AND NOT TARGET glm::glm)
	add_library(glm::glm ALIAS glm)
endif()

# Re-expose GLM's headers as SYSTEM so a consumer's strict flags (/WX /W4,
# -Werror -Wall -Wextra) don't fire on third-party template machinery. Only for
# the vendored build (glm_SOURCE_DIR is empty when satisfied by a system package).
if(glm_SOURCE_DIR AND TARGET glm)
	get_target_property(_glm_inc glm INTERFACE_INCLUDE_DIRECTORIES)
	if(_glm_inc)
		set_target_properties(glm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(glm SYSTEM INTERFACE ${_glm_inc})
	endif()
endif()
