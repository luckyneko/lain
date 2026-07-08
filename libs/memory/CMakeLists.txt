# lain::memory — foundational, dependency-free memory types (std-only). Holds the
# Buffer value type and the memory::alloc/dealloc seam it routes through. A recycling
# pool / arena is deferred (WORK.md M3): it drops in behind the alloc seam with no
# change to Buffer or its callers. No lain or third-party deps, so it sits at the
# bottom of the set alongside lain::core.

add_library(memory STATIC
	src/alloc.cpp
	src/buffer.cpp
)
add_library(lain::memory ALIAS memory)

target_compile_features(memory PUBLIC cxx_std_17)
target_include_directories(memory PUBLIC include)
target_link_libraries(memory PRIVATE lain::warnings)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
