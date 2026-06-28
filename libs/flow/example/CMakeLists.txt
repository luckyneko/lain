# lain::flow-example — shared example nodes (the viewer's smoke scene), used by
# the [gpu] tests and by flowview. GradientNode's public API names acm:: handles,
# so archimedes is PUBLIC here — it is no longer pulled in transitively through
# lain::flow, which is now GPU-free.

add_library(flow-example STATIC
	src/gradientnode.cpp
)
add_library(lain::flow-example ALIAS flow-example)

target_compile_features(flow-example PUBLIC cxx_std_17)
target_include_directories(flow-example PUBLIC include)
target_link_libraries(flow-example
	PUBLIC lain::flow archimedes
	PRIVATE lain::warnings
)
