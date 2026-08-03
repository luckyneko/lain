// PinKey — which evaluation, which node, which port. Driver-free: it names no GPU or ImGui type,
// which is why the key can be pinned down here rather than only by looking at thumbnails.
//
// The property that matters: two pins that agree on node and port but sit at DIFFERENT levels are
// different keys. The host used to guarantee that by clearing the preview cache on every navigation
// — a workaround for a key that said less than it meant, and the cause of M5's bug nine (previews
// showing another level's images). With the level in the key, the clear is a memory choice.

#include "pinkey.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

using flowview::GraphPath;
using flowview::PinKey;
using lain::flow::NodeId;
using lain::flow::PortAddress;
using lain::flow::PortId;

TEST_CASE("a pin on two levels is two keys", "[pinkey]")
{
	// The case that used to collide. Node ids no longer repeat across levels (ADR-0011), but two
	// EVALUATIONS of one definition — the target workload, N streams through one subgraph — have the
	// same node and port ids by design, so the level has to be in the key either way.
	const PortAddress port{NodeId::generate(), PortId{1}};
	const NodeId group = NodeId::generate();

	const PinKey atRoot{GraphPath{}, port};
	const PinKey inGroup{GraphPath{group}, port};

	REQUIRE(atRoot != inGroup);
	REQUIRE((atRoot < inGroup || inGroup < atRoot)); // strictly ordered, so a map keeps them apart

	std::map<PinKey, int> cache;
	cache[atRoot] = 1;
	cache[inGroup] = 2;
	REQUIRE(cache.size() == 2);
	REQUIRE(cache.at(atRoot) == 1); // ... and neither found the other's entry
	REQUIRE(cache.at(inGroup) == 2);
}

TEST_CASE("the same pin at the same level is one key", "[pinkey]")
{
	const PortAddress port{NodeId::generate(), PortId{3}};
	const GraphPath path{NodeId::generate(), NodeId::generate()};

	REQUIRE(PinKey{path, port} == PinKey{path, port});
	REQUIRE_FALSE(PinKey{path, port} < PinKey{path, port}); // irreflexive, as std::map needs

	std::set<PinKey> keys;
	keys.insert(PinKey{path, port});
	keys.insert(PinKey{path, port});
	REQUIRE(keys.size() == 1);
}

TEST_CASE("an input and an output pin are different keys with no direction field", "[pinkey]")
{
	// There is no direction in the key, and none is needed: a PortId is minted per NODE across both
	// sides (M6 step 2), so a node's first input and first output never share one.
	const NodeId node = NodeId::generate();
	const PinKey in{GraphPath{}, PortAddress{node, PortId{1}}};
	const PinKey out{GraphPath{}, PortAddress{node, PortId{2}}};

	REQUIRE(in != out);
	REQUIRE(in < out);
}

TEST_CASE("keys sort by level first", "[pinkey]")
{
	// So one level's entries sit together — which is what would make "drop the level being left" a
	// range rather than a scan, if the cache ever retains more than the active level.
	const NodeId a = NodeId::generate();
	const NodeId b = NodeId::generate();
	const PortAddress low{a < b ? a : b, PortId{1}};
	const PortAddress high{a < b ? b : a, PortId{1}};
	const NodeId group = NodeId::generate();

	// A HIGHER port address at the root still sorts before a LOWER one inside a group.
	REQUIRE(PinKey{GraphPath{}, high} < PinKey{GraphPath{group}, low});
}
