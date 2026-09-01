# lain::media — the frame-sequence model (M10, ADR-0018): a finite ordered list of frame
# references over one or more sources, with exactly one class per medium that decodes.
#
# It depends on NO io. Each medium's opener lives in that medium's own io seam
# (lain::io::image::openSequence, later lain::io::video::open) and constructs a FrameSource
# defined here — so the medium-neutral library never acquires a dependency on every medium,
# which is the junk drawer CONTEXT.md's loading split exists to prevent.

add_library(media STATIC
	src/framespec.cpp
	src/frameposition.cpp
	src/frameref.cpp
	src/framesource.cpp
	src/framesequence.cpp
	src/operations.cpp
)
add_library(lain::media ALIAS media)

target_compile_features(media PUBLIC cxx_std_17)
target_include_directories(media PUBLIC include)
target_link_libraries(media
	PUBLIC lain::image				# image::Image is what a frame decodes to (framesource.h)
	PUBLIC lain::core				# core::Time is a frame's timestamp (frameref.h)
	PRIVATE lain::log				# refusal diagnostics
	PRIVATE lain::string			# toString formatting
	PRIVATE lain::warnings
)

# The documents describing what this library models. A separate target because serialization must
# name lain::data, and media itself stays data-free (the flow / flow::serialize split, applied here).
add_subdirectory(serialize)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
