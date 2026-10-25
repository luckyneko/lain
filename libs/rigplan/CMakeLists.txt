# lain::rigplan — rig design's model (WORK.md M15, ADR-0029): the specifications a rig is built from,
# the rig plan, and the realisation a synthetic capture renders. Everything that decides what a
# capture MEANS lives here, with no GPU and no document format: serialization is lain::rigplan::
# serialize's, rendering lain::render's.

add_library(rigplan STATIC
	src/specification.cpp
	src/rigplan.cpp
	src/realisation.cpp
	src/environment.cpp
	src/sensor.cpp
	src/mover.cpp
	src/truth.cpp
)
add_library(lain::rigplan ALIAS rigplan)

target_compile_features(rigplan PUBLIC cxx_std_17)
target_include_directories(rigplan PUBLIC include)
target_link_libraries(rigplan
	PUBLIC lain::camera lain::core lain::image lain::math lain::media
	PRIVATE lain::string lain::task lain::warnings
)

# The sensor model's codes must be the same on every machine (ADR-0029), so its arithmetic is one IEEE
# operation at a time: no fused multiply-add, which clang emits for a * b + c on arm64 and not on
# x86-64. MSVC's default /fp:precise does not contract.
set_source_files_properties(src/sensor.cpp PROPERTIES
	COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>"
)

add_subdirectory(serialize) # rig plans and catalogues as documents

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
