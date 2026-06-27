# lain::flow — the node-graph engine. PortValue's public API names acm:: handles,
# so archimedes is a PUBLIC dependency.

add_library(flow STATIC
	src/portvalue.cpp
	src/graph.cpp
	src/scheduler.cpp
)
add_library(lain::flow ALIAS flow)

target_compile_features(flow PUBLIC cxx_std_17)
target_include_directories(flow PUBLIC include)
# lain::task is PRIVATE: only the scheduler / Graph .cpp use it; graph.h merely
# forward-declares task::Executor. archimedes is PUBLIC — its handles are in
# PortValue's API.
target_link_libraries(flow
	PUBLIC archimedes
	PRIVATE lain::task lain::warnings
)

add_subdirectory(example) # shared example nodes (flowview's smoke scene)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
