# lain::io::data — the codec-free serialization-loader seam: the DataReader / DataWriter
# interfaces, the reader/writer registries (core::Factory), and the load/decode + save/encode
# facade over a lain::data::Value. Holds NO third-party parser. Real format codecs (json via
# nlohmann, later yaml/xml) are separate plugins under plugins/io/data/<fmt> that depend on this
# seam and register into the registries. A nested sub-target of libs/io, peer to lain::io::image.

add_library(io-data STATIC
	src/load.cpp
	src/save.cpp
)
add_library(lain::io::data ALIAS io-data)

target_compile_features(io-data PUBLIC cxx_std_17)
target_include_directories(io-data PUBLIC include)
target_link_libraries(io-data
	PUBLIC lain::data lain::memory lain::core
	PRIVATE lain::io lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
