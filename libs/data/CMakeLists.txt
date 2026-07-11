# lain::data — the format-neutral serialization DOM (Value) + the T <-> Value reflection layer
# (toValue / fromValue over a single serialize() visitor). Names no format and no payload type —
# codecs (lain::io::data) turn a Value to/from bytes, consumers declare serialize() for their
# types. std-only in this first slice, so it sits low in the set beside lain::core / lain::memory.
# (Next slice pulls in lain::memory for a Bytes arm and lain::meta for enum-as-name.)

add_library(data STATIC
	src/value.cpp
)
add_library(lain::data ALIAS data)

target_compile_features(data PUBLIC cxx_std_17)
target_include_directories(data PUBLIC include)
target_link_libraries(data PRIVATE lain::warnings)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
