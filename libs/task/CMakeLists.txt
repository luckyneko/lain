# lain::task — a namespace ALIAS for multi (lain's own work-stealing pool), not a wrapper.
# One line of header: `namespace lain { namespace task = multi; }`. multi is owned, so there is
# no upstream vocabulary to keep out — the same reason acm:: appears raw in lain::app's and
# lain::gui's public headers. See docs/adr/0024-one-process-task-pool.md.
#
# The target survives the wrapper's deletion because it is what carries the link: multi::multi
# is a static library, so a consumer links one where header-only Taskflow linked nothing.

add_library(lain_task INTERFACE)
add_library(lain::task ALIAS lain_task)

target_compile_features(lain_task INTERFACE cxx_std_17)
target_include_directories(lain_task INTERFACE include)
target_link_libraries(lain_task INTERFACE multi::multi)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
