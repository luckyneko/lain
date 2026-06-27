# Provides the Taskflow::Taskflow target (the work-stealing task-graph executor
# behind lain::task).
#
# Prefers a system package; otherwise fetches a pinned release via FetchContent.
# The download path mirrors the source URL under .cache/fetch/ so similarly
# named archives across deps can't collide; sources extract into build/_deps.
# Taskflow is header-only, so this contributes an INTERFACE target only.

include(FetchContent)

# Pinned for a C++17 / MSVC-clean build:
#  - Taskflow 4.x requires C++20 (cxx_std_20), which would propagate to every
#    lain::task consumer; lain is a C++17 set.
#  - 3.8.0+ unconditionally `#include <latch>` (a C++20 header) in
#    utility/traits.hpp, which MSVC warns on under C++17 (STL4038) -> our /WX.
# 3.7.0 is the newest release before that include; the static task-graph API we
# use (Taskflow / Task::precede / Executor::run) has been stable since 2.x.
set(TASKFLOW_VER "3.7.0")
set(TASKFLOW_FILE "github.com/taskflow/taskflow/archive/v${TASKFLOW_VER}.tar.gz")

# We only want the headers — keep Taskflow's own tests/examples/profiler/CUDA
# out of our build graph.
set(TF_BUILD_TESTS OFF CACHE BOOL "Disable Taskflow tests" FORCE)
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "Disable Taskflow examples" FORCE)
set(TF_BUILD_PROFILER OFF CACHE BOOL "Disable Taskflow profiler" FORCE)
set(TF_BUILD_CUDA OFF CACHE BOOL "Disable Taskflow CUDA" FORCE)

FetchContent_Declare(Taskflow
	URL          "https://${TASKFLOW_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${TASKFLOW_FILE}"
	FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(Taskflow)

# Consumed via FetchContent, Taskflow exports the bare `Taskflow` target; the
# namespaced `Taskflow::Taskflow` alias only ships with its installed package
# config. Normalize so lain::task can always link `Taskflow::Taskflow`.
if(TARGET Taskflow AND NOT TARGET Taskflow::Taskflow)
	add_library(Taskflow::Taskflow ALIAS Taskflow)
endif()

# The hardening below applies only to the vendored build (Taskflow_SOURCE_DIR is
# empty when the dependency was satisfied by a system package).
if(Taskflow_SOURCE_DIR AND TARGET Taskflow)
	# Re-expose Taskflow's headers as SYSTEM so a consumer's -Werror/-Wall/-Wextra
	# (and /WX /W4) don't fire on third-party headers.
	get_target_property(_tf_inc Taskflow INTERFACE_INCLUDE_DIRECTORIES)
	if(_tf_inc)
		set_target_properties(Taskflow PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
		target_include_directories(Taskflow SYSTEM INTERFACE ${_tf_inc})
	endif()
endif()
