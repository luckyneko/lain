// Test for the generated codec aggregator. registerImageCodecs() must register exactly the
// codecs this build has into the reader registry (LAIN_HAS_<FMT>, from the plugin targets). Proves
// the build-discovered aggregator wires a codec end to end, and that one turned OFF stays out.

#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h> // readerRegistry

#include <catch2/catch_test_macros.hpp>

TEST_CASE("registerImageCodecs registers the built-in codecs", "[io-image-codecs]")
{
	lain::io::image::registerImageCodecs();
	CHECK(lain::io::image::readerRegistry().contains("png") == bool(LAIN_HAS_PNG));
	CHECK(lain::io::image::readerRegistry().contains("tiff") == bool(LAIN_HAS_TIFF));
	CHECK(lain::io::image::readerRegistry().contains("jpg") == bool(LAIN_HAS_JPEG));
}
