# lain::flow::serialize — Graph <-> data::Value (WORK.md Tier A #1). A separate target over flow's
# public API + lain::data, kept OUT of flow core by the boundary rule: a concern lives in flow core
# iff it stays payload-agnostic, and serialization must name lain::data + concrete payload types (via
# the app-populated ValueCodecs). Peer in spirit to flow::edit, but out-of-core for the dependency.

add_library(flow-serialize STATIC
	src/valuecodecs.cpp
	src/serialize.cpp
)
add_library(lain::flow::serialize ALIAS flow-serialize)

target_compile_features(flow-serialize PUBLIC cxx_std_17)
target_include_directories(flow-serialize PUBLIC include)
target_link_libraries(flow-serialize
	PUBLIC lain::flow lain::data lain::core # core::Factory<Node> is in the public toValue/fromValue signature
	PRIVATE lain::log lain::warnings		# the load path logs each issue as it records it
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
