# Builds the `imnodes` static library (Nelarius/imnodes) — a small node-graph canvas
# for Dear ImGui, used by flowview's node view. Like imgui, we define the target
# ourselves so it is built against our imgui target (and therefore the same
# IMGUI_USER_CONFIG / ImVec bridge — one definition across the program). imnodes ships
# its own CMakeLists, so SOURCE_SUBDIR points at a nonexistent dir to skip its
# add_subdirectory and just populate the sources.
#
# imnodes has no release that tracks ImGui 1.92, so we pin a master commit whose
# imnodes.cpp branches on IMGUI_VERSION_NUM >= 19200 (the 1.92 texture-stack change).

include(FetchContent)

set(IMNODES_REF "eb36902c892548ef94f88f51ad7e7c9c7058a71c" CACHE STRING "Vendored imnodes commit")
set(IMNODES_FILE "github.com/Nelarius/imnodes/archive/${IMNODES_REF}.tar.gz")

FetchContent_Declare(imnodes
	URL           "https://${IMNODES_FILE}"
	DOWNLOAD_DIR  "${CMAKE_SOURCE_DIR}/.cache/fetch/${IMNODES_FILE}"
	SOURCE_SUBDIR "skip-imnodes-cmakelists" # nonexistent -> populate only, we define the target
)
FetchContent_MakeAvailable(imnodes)

add_library(imnodes STATIC
	"${imnodes_SOURCE_DIR}/imnodes.cpp"
)
add_library(imnodes::imnodes ALIAS imnodes)

target_compile_features(imnodes PUBLIC cxx_std_17)
target_include_directories(imnodes SYSTEM PUBLIC "${imnodes_SOURCE_DIR}")
# imnodes is ImGui code: it needs the imgui headers + our IMGUI_USER_CONFIG, both of
# which ride in on imgui's PUBLIC usage requirements (the config is applied by libs/gui).
target_link_libraries(imnodes PUBLIC imgui)
