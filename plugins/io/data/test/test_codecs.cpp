// Test for the generated codec aggregator. registerDataCodecs() must register the codecs enabled at
// build time into the reader/writer registries; with the default configuration that includes JSON.
// Proves the build-discovered aggregator wires a codec end to end.

#include <lain/io/data/codecs.h>
#include <lain/io/data/load.h> // readerRegistry
#include <lain/io/data/save.h> // writerRegistry

#include <catch2/catch_test_macros.hpp>

TEST_CASE("registerDataCodecs registers the built-in codecs", "[io-data-codecs]")
{
	lain::io::data::registerDataCodecs();
	REQUIRE(lain::io::data::readerRegistry().contains("json"));
	REQUIRE(lain::io::data::writerRegistry().contains("json"));
}
