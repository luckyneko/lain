// Tests for the group-node seam (group.h): a node that CONTAINS a graph. This slice is the data
// model only — the scheduler does not expand groups yet (that is the execution-plan slice), so what
// is proven here is ownership, the structural innerGraph() question the scheduler will ask, dirty
// propagation across the boundary, and the linked variant's source + interface cache.

#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "testnodes.h" // ConstInt — an ordinary node to put inside a group

#include <catch2/catch_test_macros.hpp>

using namespace lain::flow;

TEST_CASE("a group owns an inner graph, which is born with its own interface", "[flow][group]")
{
	GroupNode group;
	REQUIRE(group.inner().nodeCount() == 2); // the inner boundary pair — the group's own interface

	// The structural question the scheduler asks every node (it never dynamic_casts for this).
	REQUIRE(group.innerGraph() == &group.inner());

	const Node& plain = group.inner().boundaryInputNode();
	REQUIRE(plain.innerGraph() == nullptr); // an ordinary node contains nothing
}

TEST_CASE("two groups never share an inner graph", "[flow][group]")
{
	// A link references a RECIPE, never a running graph: a Port holds a persistent value, so a
	// shared inner graph would have two instances stomping each other's intermediates.
	GroupNode a;
	GroupNode b;
	REQUIRE(a.innerGraph() != b.innerGraph());

	a.inner().add<test::ConstInt>(1);
	REQUIRE(a.inner().nodeCount() == 3);
	REQUIRE(b.inner().nodeCount() == 2); // untouched
}

TEST_CASE("a group is dirty when anything inside it is", "[flow][group]")
{
	GroupNode group;
	const NodeId inner = group.inner().add<test::ConstInt>(1);

	group.clearDirty();
	REQUIRE(group.dirty()); // the freshly added inner node is dirty, so the group is

	// Clear the whole inner graph, and the group goes clean.
	for (const NodeId id : group.inner().nodeIds())
		group.inner().node(id).clearDirty();
	REQUIRE_FALSE(group.dirty());

	SECTION("an edit inside reaches the outer dirty state")
	{
		group.inner().node(inner).markDirty();
		REQUIRE(group.dirty());
	}

	SECTION("the group's own dirty flag still counts on its own")
	{
		group.markDirty();
		REQUIRE(group.dirty());
	}

	SECTION("it recurses through nested groups")
	{
		// A group inside a group: the innermost edit must surface at the outermost node.
		GroupNode outer;
		const NodeId nestedId = outer.inner().add<GroupNode>();
		auto& nested = static_cast<GroupNode&>(outer.inner().node(nestedId));
		const NodeId deep = nested.inner().add<test::ConstInt>(2);

		outer.clearDirty();
		for (const NodeId id : outer.inner().nodeIds())
			outer.inner().node(id).clearDirty();
		for (const NodeId id : nested.inner().nodeIds())
			nested.inner().node(id).clearDirty();
		REQUIRE_FALSE(outer.dirty());

		nested.inner().node(deep).markDirty();
		REQUIRE(outer.dirty());
	}
}

TEST_CASE("a group maps its outer ports to inner boundary pins by id", "[flow][group]")
{
	// Identity is the PortId pair, not the name — renaming an inner pin must keep the outer wiring.
	GroupNode group;
	const PortId innerPin = group.inner().boundaryInputNode().addBoundary<int>("source");
	const PortId outerPin{7}; // stands in for the port edit::syncGroupPorts will create

	REQUIRE(group.innerPin(outerPin) == PortId{}); // unmapped until synced
	group.mapPort(outerPin, innerPin);
	REQUIRE(group.innerPin(outerPin) == innerPin);

	// A rename does not disturb the mapping — that is the whole point of addressing by id.
	group.inner().boundaryInputNode().findOutput(innerPin)->setName("renamed");
	REQUIRE(group.innerPin(outerPin) == innerPin);

	group.unmapPort(outerPin);
	REQUIRE(group.innerPin(outerPin) == PortId{});
}

TEST_CASE("a linked group carries its template path and cached interface", "[flow][group]")
{
	LinkedGroupNode linked;
	REQUIRE(linked.innerGraph() != nullptr); // same inner-graph machinery as an inline group
	REQUIRE(linked.source().empty());
	REQUIRE_FALSE(linked.resolved()); // nothing loaded yet

	linked.setSource("subs/denoise.json");
	linked.setCachedInterface({PinSpec{"source", "Image"}}, {PinSpec{"result", "Image"}});
	linked.setResolved(true);

	REQUIRE(linked.source() == "subs/denoise.json");
	REQUIRE(linked.cachedInputs().size() == 1);
	REQUIRE(linked.cachedInputs()[0].name == "source");
	REQUIRE(linked.cachedInputs()[0].typeKey == "Image"); // a port-type registry key
	REQUIRE(linked.cachedOutputs()[0].name == "result");
	REQUIRE(linked.resolved());
}
