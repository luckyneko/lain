# lain::image — CPU-side raster + pixel formats. A foundational, archimedes/UI-agnostic
# image type over three featherweight foundations: lain::math (Vec2i extent, Rect2i windows),
# lain::meta (enum-named formats, and the enum-indexed type tables) and lain::memory (the
# pixel buffer). A sibling camera-geometry library (intrinsics/distortion) will build on this,
# so the raster foundation deliberately stays lightweight.

add_library(image STATIC
	src/image.cpp
	src/convert.cpp
	src/operations.cpp
)
add_library(lain::image ALIAS image)

target_compile_features(image PUBLIC cxx_std_17)
target_include_directories(image PUBLIC include)
target_link_libraries(image
	PUBLIC lain::math				# math::Vec2i in the public extent()
	PUBLIC lain::meta				# meta::TypeList in pixelformat.h/color.h (public headers)
	PUBLIC lain::memory				# memory::Buffer is Image's storage (image.h)
	PRIVATE lain::log				# enforcement diagnostics (convert.cpp / operations.cpp)
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
