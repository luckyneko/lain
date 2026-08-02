# lain::core — foundational, dependency-free types and utilities (std-only).
# Holds Time, Version, Uuid, Factory and the path/platform helpers; a wall-clock DateTime
# and a video Timecode are deferred (WORK.md).
# No lain or third-party deps, so any library above may build on it.

add_library(core STATIC
	src/time.cpp
	src/version.cpp
	src/paths.cpp
	src/platform.cpp
	src/uuid.cpp
)
add_library(lain::core ALIAS core)

target_compile_features(core PUBLIC cxx_std_17)
target_include_directories(core PUBLIC include)
target_link_libraries(core PRIVATE lain::warnings)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
