# lain::task — a thin, header-only wrapper over Taskflow. Exposes only the small
# surface flow's scheduler uses: a Flow that emplaces node-tasks and adds edges,
# and an Executor that runs one to completion. Nothing else of Taskflow leaks
# past lain::task (no tf:: in a consumer's translation unit).

add_library(lain_task INTERFACE)
add_library(lain::task ALIAS lain_task)

target_compile_features(lain_task INTERFACE cxx_std_17)
target_include_directories(lain_task INTERFACE include)
target_link_libraries(lain_task INTERFACE Taskflow::Taskflow)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
