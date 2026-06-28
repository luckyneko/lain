# lain::flow — the node-graph engine. Payload-agnostic: PortValue is a plain
# std::any slot, so flow names no GPU types and depends only on lain::task.

add_library(flow STATIC
	src/portvalue.cpp
	src/graph.cpp
	src/scheduler.cpp
)
add_library(lain::flow ALIAS flow)

target_compile_features(flow PUBLIC cxx_std_17)
target_include_directories(flow PUBLIC include)
# lain::task is PRIVATE: only the scheduler / Graph .cpp use it; graph.h merely
# forward-declares task::Executor. No GPU dependency — a node that emits an
# acm:: handle (and the viewer that previews it) links archimedes itself.
target_link_libraries(flow
	PRIVATE lain::task lain::warnings
)

add_subdirectory(example) # shared example nodes (flowview's smoke scene)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
