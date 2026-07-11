# lain::io::data::json — the JSON codec plugin. A JsonReader/JsonWriter mapping a lain::data::Value
# to/from JSON text via nlohmann/json, registered into the io::data registries through registerCodec.
# A swappable satellite of the parser-free lain::io::data seam: nlohmann lives only here (fetched by
# addnlohmann only because this plugin is enabled), never in core io::data.

include(addnlohmann) # nlohmann_json::nlohmann_json (fetches nlohmann/json on demand)

add_library(io-data-json STATIC
	src/json.cpp
)
add_library(lain::io::data::json ALIAS io-data-json)

target_compile_features(io-data-json PUBLIC cxx_std_17)
target_include_directories(io-data-json PUBLIC include)
target_link_libraries(io-data-json
	PUBLIC lain::io::data
	PRIVATE nlohmann_json::nlohmann_json lain::warnings
)

# Advertise this codec to the generated aggregator (plugins/io/data/CMakeLists.txt). The entry is
# the alias target *and* the namespace — the aggregator links it and, from the same string, derives
# its <lain/io/data/json/register.h> include and registerCodec() call.
set_property(GLOBAL APPEND PROPERTY LAIN_DATA_CODECS lain::io::data::json)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
