# lain::flow-example — shared example nodes (the viewer's smoke scene), used by the flow
# node tests and by flowview. GradientNode/TintNode are pure-CPU now: they produce/consume
# a lain::image::Image, so this library is GPU-free (no archimedes). A future GPU/compute
# node would reintroduce an archimedes dependency + an injected device context.

add_library(flow-example STATIC
	src/gradientnode.cpp
	src/tintnode.cpp
	src/blurnode.cpp
	src/loadimagenode.cpp
	src/listdirnode.cpp
	src/combinenode.cpp
	src/opensequencenode.cpp
	src/frameatnode.cpp
	src/clipsequencenode.cpp
)
add_library(lain::flow-example ALIAS flow-example)

target_compile_features(flow-example PUBLIC cxx_std_17)
target_include_directories(flow-example PUBLIC include)
target_link_libraries(flow-example
	PUBLIC lain::flow lain::image lain::media
	# LoadImageNode uses io::image::load (impl only); CombineNode says WHY it refused a ragged
	# collection — flow CORE stays log-free, but an example node is app-adjacent and a silent
	# refusal reads as a bug.
	PRIVATE lain::io::image lain::io::sequence lain::log lain::warnings
)
