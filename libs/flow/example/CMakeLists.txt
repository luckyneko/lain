# lain::flow-example — shared example nodes (the viewer's smoke scene), used by the flow
# node tests and by flowview. GradientNode/TintNode are pure-CPU now: they produce/consume
# a lain::image::Image, so this library is GPU-free (no archimedes). A future GPU/compute
# node would reintroduce an archimedes dependency + an injected device context.

add_library(flow-example STATIC
	src/gradientnode.cpp
	src/tintnode.cpp
	src/blurnode.cpp
)
add_library(lain::flow-example ALIAS flow-example)

target_compile_features(flow-example PUBLIC cxx_std_17)
target_include_directories(flow-example PUBLIC include)
target_link_libraries(flow-example
	PUBLIC lain::flow lain::image
	PRIVATE lain::warnings
)
