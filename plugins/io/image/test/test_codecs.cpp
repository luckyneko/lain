// Test for the generated codec aggregator. registerImageCodecs() must register the
// codecs enabled at build time into the reader registry; with the default configuration
// that includes PNG. Proves the build-discovered aggregator wires a codec end to end.

#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h> // readerRegistry

#include <catch2/catch_test_macros.hpp>

TEST_CASE("registerImageCodecs registers the built-in codecs", "[io-image-codecs]")
{
	lain::io::image::registerImageCodecs();
	// PNG ships enabled by default (LAIN_IO_IMAGE_PNG); the aggregator must have wired it.
	REQUIRE(lain::io::image::readerRegistry().contains("png"));
}
