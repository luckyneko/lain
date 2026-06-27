# lain::flow — the node-graph engine. PortValue's public API names acm:: handles,
# so archimedes is a PUBLIC dependency.

add_library(flow STATIC
	src/portvalue.cpp
	src/graph.cpp
)
add_library(lain::flow ALIAS flow)

target_compile_features(flow PUBLIC cxx_std_17)
target_include_directories(flow PUBLIC include)
target_link_libraries(flow
	PUBLIC archimedes
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
