# lain::flow-example — shared example nodes (the viewer's smoke scene), used by
# the [gpu] tests and by flowview. Depends on flow + archimedes via lain::flow.

add_library(flow-example STATIC
	src/gradientnode.cpp
)
add_library(lain::flow-example ALIAS flow-example)

target_compile_features(flow-example PUBLIC cxx_std_17)
target_include_directories(flow-example PUBLIC include)
target_link_libraries(flow-example
	PUBLIC lain::flow
	PRIVATE lain::warnings
)
