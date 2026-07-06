# lain::image — CPU-side raster + pixel formats. A foundational, archimedes/UI-agnostic
# image type over two featherweight foundations: lain::math (Vec2i extent; color-space
# conversion matrices arrive with image::convert()) and lain::meta (enum-named formats).
# A sibling camera-geometry library (intrinsics/distortion) will build on this, so the
# raster foundation deliberately stays lightweight.

add_library(image STATIC
	src/image.cpp
)
add_library(lain::image ALIAS image)

target_compile_features(image PUBLIC cxx_std_17)
target_include_directories(image PUBLIC include)
target_link_libraries(image
	PUBLIC lain::math				 # math::Vec2i in the public extent()
	PRIVATE lain::meta lain::warnings # meta::enums::name in toString (impl only)
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
