// Test for the generated codec aggregator. registerDataCodecs() must register exactly the codecs this
// build has into the reader/writer registries (LAIN_HAS_JSON, from the plugin target). Proves the
// build-discovered aggregator wires a codec end to end, and that one turned OFF stays out.

#include <lain/io/data/codecs.h>
#include <lain/io/data/load.h> // readerRegistry
#include <lain/io/data/save.h> // writerRegistry

#include <catch2/catch_test_macros.hpp>

TEST_CASE("registerDataCodecs registers the built-in codecs", "[io-data-codecs]")
{
	lain::io::data::registerDataCodecs();
	CHECK(lain::io::data::readerRegistry().contains("json") == bool(LAIN_HAS_JSON));
	CHECK(lain::io::data::writerRegistry().contains("json") == bool(LAIN_HAS_JSON));
}
