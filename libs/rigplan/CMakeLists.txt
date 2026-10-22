# lain::rigplan — rig design's model (WORK.md M15, ADR-0029): the specifications a rig is built from,
# the rig plan, and the realisation a synthetic capture renders. Everything that decides what a
# capture MEANS lives here, with no GPU and no document format: serialization is lain::rigplan::
# serialize's, rendering lain::render's.

add_library(rigplan STATIC
	src/specification.cpp
	src/rigplan.cpp
	src/realisation.cpp
)
add_library(lain::rigplan ALIAS rigplan)

target_compile_features(rigplan PUBLIC cxx_std_17)
target_include_directories(rigplan PUBLIC include)
target_link_libraries(rigplan
	PUBLIC lain::camera lain::core lain::image lain::math lain::media
	PRIVATE lain::string lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
