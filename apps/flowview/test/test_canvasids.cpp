// CanvasIds — the imnodes int <-> domain-id mapping. Driver-free: the mapping itself names no ImGui
// or imnodes type, which is why it can be pinned down here rather than only on a live canvas.
//
// What the ints have to guarantee, and why (ADR-0011): imnodes keeps per-object state — position,
// selection, drag — keyed by them, so an int that later names a DIFFERENT object hands the newcomer
// the old one's state.

#include "canvasids.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using flowview::CanvasIds;
using lain::flow::NodeId;
using lain::flow::PortAddress;
using lain::flow::PortId;

TEST_CASE("an object's canvas id is stable and round-trips", "[canvasids]")
{
	CanvasIds ids;
	const NodeId node = NodeId::generate();
	const PortAddress port{node, PortId{1}};

	REQUIRE(ids.node(node) == ids.node(node)); // asking twice is asking about the same object
	REQUIRE(ids.pin(port, true) == ids.pin(port, true));
	REQUIRE(ids.link(port) == ids.link(port));

	REQUIRE(ids.toNode(ids.node(node)) == node);
	REQUIRE(ids.toPin(ids.pin(port, true))->address == port);
	REQUIRE(ids.toPin(ids.pin(port, true))->output);
	REQUIRE(ids.toLink(ids.link(port)) == port);
}

TEST_CASE("a pin's direction is part of its identity", "[canvasids]")
{
	// A PortId is minted per NODE, so a node's first input and its first output are both PortId{1} —
	// the address alone does not name a pin.
	CanvasIds ids;
	const PortAddress address{NodeId::generate(), PortId{1}};

	REQUIRE(ids.pin(address, false) != ids.pin(address, true));
	REQUIRE_FALSE(ids.toPin(ids.pin(address, false))->output);
	REQUIRE(ids.toPin(ids.pin(address, true))->output);
}

TEST_CASE("nodes, pins and links never share an int", "[canvasids]")
{
	// imnodes pools the three kinds separately, so it would tolerate an overlap — one counter across
	// all of them means a stray id is a lookup MISS rather than a silent hit on the wrong kind.
	CanvasIds ids;
	const NodeId node = NodeId::generate();
	const PortAddress address{node, PortId{1}};

	const std::set<int> allocated{ids.node(node), ids.pin(address, false), ids.pin(address, true), ids.link(address)};
	REQUIRE(allocated.size() == 4);

	REQUIRE_FALSE(ids.toNode(ids.pin(address, false)));
	REQUIRE_FALSE(ids.toPin(ids.node(node)));
	REQUIRE_FALSE(ids.toLink(ids.node(node)));
}

TEST_CASE("an int is never recycled onto another object", "[canvasids]")
{
	// The whole point: a deleted node's int must not be handed to the next one, or imnodes would give
	// the newcomer the deleted node's position and selection state. Ids are monotonic and nothing
	// removes them, so this holds by construction — and it is what a dense per-frame table gets wrong.
	CanvasIds ids;
	std::set<int> seen;
	for (int i = 0; i < 100; ++i)
		REQUIRE(seen.insert(ids.node(NodeId::generate())).second);
	REQUIRE(seen.size() == 100);
}

TEST_CASE("an unknown int resolves to nothing", "[canvasids]")
{
	// Callers must tolerate this: imnodes reports hovered / dragged / destroyed objects after the
	// fact, by which point the graph — or the whole document — may have moved on.
	CanvasIds ids;
	REQUIRE_FALSE(ids.toNode(0));
	REQUIRE_FALSE(ids.toNode(12345));
	REQUIRE_FALSE(ids.toPin(12345));
	REQUIRE_FALSE(ids.toLink(12345));
}

TEST_CASE("reset forgets the document", "[canvasids]")
{
	CanvasIds ids;
	const NodeId node = NodeId::generate();
	const int before = ids.node(node);

	ids.reset();
	REQUIRE_FALSE(ids.toNode(before)); // the old document's ints name nothing

	// A node carried across the reset (the same file re-opened, say) simply gets a new int; what
	// matters is that the mapping is consistent afterwards, not that it agrees with the old one.
	REQUIRE(ids.toNode(ids.node(node)) == node);
}
