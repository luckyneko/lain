// The generated mesh codec aggregator: registerMeshCodecs() registers exactly the codecs this build has
// (LAIN_HAS_OBJ, from the plugin target), so one turned OFF stays out.

#include <lain/io/mesh/codecs.h>
#include <lain/io/mesh/load.h> // readerRegistry

#include <catch2/catch_test_macros.hpp>

TEST_CASE("registerMeshCodecs registers the built-in codecs", "[io-mesh-codecs]")
{
	lain::io::mesh::registerMeshCodecs();
	CHECK(lain::io::mesh::readerRegistry().contains("obj") == bool(LAIN_HAS_OBJ));
}
