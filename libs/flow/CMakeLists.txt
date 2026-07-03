# lain::flow — the node-graph engine. Payload-agnostic: PortValue is a plain
# std::any slot, so flow names no GPU types. Depends on lain::task (execution) and
# lain::meta (a port captures lain::meta::typeName<T>() for the inspector) — both
# featherweight; no GPU/UI coupling.

add_library(flow STATIC
	src/portvalue.cpp
	src/graph.cpp
	src/scheduler.cpp
	src/edit.cpp
)
add_library(lain::flow ALIAS flow)

target_compile_features(flow PUBLIC cxx_std_17)
target_include_directories(flow PUBLIC include)
# lain::meta is PUBLIC: node.inl (a header consumers compile when they declare ports)
# calls lain::meta::typeName<T>(). lain::task is PRIVATE: only the scheduler / Graph
# .cpp use it; graph.h merely forward-declares task::Executor. No GPU dependency — a
# node that emits an acm:: handle (and the viewer that previews it) links archimedes.
target_link_libraries(flow
	PUBLIC lain::meta
	PRIVATE lain::task lain::warnings
)

add_subdirectory(example) # shared example nodes (flowview's smoke scene)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
