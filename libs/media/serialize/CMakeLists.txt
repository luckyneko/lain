# lain::media::serialize — the frame-sequence model's documents (WORK.md M10).
#
# A separate target over lain::media + lain::data, kept OUT of media by the boundary rule
# flow::serialize already follows: a concern lives in the core library iff it stays free of an
# external format, and the moment it must name lain::data it lives outside. So lain::media keeps
# linking no data — as no foundational library in the set does — while what a sequence's honest
# description IS stays with the sequence model instead of in whichever app writes one.

add_library(media-serialize STATIC
	src/manifest.cpp
)
add_library(lain::media::serialize ALIAS media-serialize)

target_compile_features(media-serialize PUBLIC cxx_std_17)
target_include_directories(media-serialize PUBLIC include)
target_link_libraries(media-serialize
	PUBLIC lain::media				# it describes a FrameSequence
	PUBLIC lain::data				# and data::Value/Archive appear in the public surface
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
