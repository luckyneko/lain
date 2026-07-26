// Tests for the dynamic-port mutation engine (dynamicports.h + Graph::removePort +
// edit::removePort): pins added at runtime carry stable PortIds, the Graph primitive refuses to
// drop a still-connected pin, and the edit gesture disconnects-then-removes. The payoff test is
// mid-list removal: dropping one pin must leave the others' ids + edges untouched. Driver-free
// (int payload).

#include "lain/flow/dynamicports.h"
#include "lain/flow/edit.h"
#include "lain/flow/graph.h"
#include "lain/flow/porttyperegistry.h"
#include "testnodes.h" // ConstInt — an int source to wire into a dynamic pin

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace lain::flow;

namespace
{
	// A test node with dynamic INPUT pins of int (stands in for a Merge / a boundary node).
	struct DynInts : DynamicPortsNode
	{
		DynInts()
			: DynamicPortsNode("DynInts")
		{
		}
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }
		void compute() override {}
	};
} // namespace

TEST_CASE("dynamic pins are added at runtime with distinct stable ids", "[flow][dynamic]")
{
	Graph g;
	const NodeId n = g.add<DynInts>();
	auto& dyn = static_cast<DynInts&>(g.node(n));

	const PortId a = dyn.addDynamicPort<int>("in0");
	const PortId b = dyn.addDynamicPort<int>("in1");
	REQUIRE(dyn.inputCount() == 2);
	REQUIRE(a != b);
	REQUIRE(dyn.findInput(a) != nullptr);
	REQUIRE(dyn.findInput(a)->name() == "in0");
}

TEST_CASE("Graph::removePort refuses a connected pin, edit::removePort clears it", "[flow][dynamic]")
{
	Graph g;
	const NodeId c = g.add<test::ConstInt>(7);
	const NodeId n = g.add<DynInts>();
	auto& dyn = static_cast<DynInts&>(g.node(n));
	const PortId pin = dyn.addDynamicPort<int>("in0");
	REQUIRE(g.connect(c, 0, n, 0) == Connection::Ok);

	// The primitive refuses while the pin is wired — the invariant holds.
	REQUIRE_FALSE(g.removePort(PortAddress{n, pin}));
	REQUIRE(dyn.inputCount() == 1);
	REQUIRE(g.edges().size() == 1);

	// The edit gesture disconnects the incident edge, then removes.
	REQUIRE(edit::removePort(g, PortAddress{n, pin}));
	REQUIRE(dyn.inputCount() == 0);
	REQUIRE(g.edges().empty());
}

TEST_CASE("Graph::removePort drops a free pin", "[flow][dynamic]")
{
	Graph g;
	const NodeId n = g.add<DynInts>();
	auto& dyn = static_cast<DynInts&>(g.node(n));
	const PortId pin = dyn.addDynamicPort<int>("in0");

	REQUIRE(g.removePort(PortAddress{n, pin})); // no edges -> primitive succeeds
	REQUIRE(dyn.inputCount() == 0);
}

TEST_CASE("removing a middle pin leaves the others' ids and edges intact", "[flow][dynamic]")
{
	// The payoff of stable PortId: three wired pins, drop the middle one, the outer two keep
	// their ids and their edges — an index-based model would have corrupted them.
	Graph g;
	const NodeId c0 = g.add<test::ConstInt>(10);
	const NodeId c1 = g.add<test::ConstInt>(20);
	const NodeId c2 = g.add<test::ConstInt>(30);
	const NodeId n = g.add<DynInts>();
	auto& dyn = static_cast<DynInts&>(g.node(n));
	const PortId p0 = dyn.addDynamicPort<int>("in0");
	const PortId p1 = dyn.addDynamicPort<int>("in1");
	const PortId p2 = dyn.addDynamicPort<int>("in2");
	REQUIRE(g.connect(c0, 0, n, 0) == Connection::Ok);
	REQUIRE(g.connect(c1, 0, n, 1) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, n, 2) == Connection::Ok);

	// Drop the middle pin (p1) — the vector compacts, but ids don't shift.
	REQUIRE(edit::removePort(g, PortAddress{n, p1}));
	REQUIRE(dyn.inputCount() == 2);
	REQUIRE(dyn.findInput(p1) == nullptr); // gone
	REQUIRE(dyn.findInput(p0) != nullptr); // survivors keep their ids
	REQUIRE(dyn.findInput(p2) != nullptr);

	// The outer two edges survive, still addressed to p0 / p2.
	REQUIRE(g.edges().size() == 2);
	bool toP0 = false;
	bool toP2 = false;
	for (const Graph::Edge& e : g.edges())
	{
		if (e.to == PortAddress{n, p0})
			toP0 = true;
		if (e.to == PortAddress{n, p2})
			toP2 = true;
	}
	REQUIRE(toP0);
	REQUIRE(toP2);
}

TEST_CASE("the port-type registry adds a pin of a registered type", "[flow][dynamic]")
{
	registerPortType<int>("int"); // the app registers image::Image etc. the same way
	REQUIRE(portTypeRegistered("int"));
	const auto keys = portTypeKeys();
	REQUIRE(std::find(keys.begin(), keys.end(), "int") != keys.end());

	Graph g;
	const NodeId n = g.add<DynInts>();
	auto& dyn = static_cast<DynInts&>(g.node(n));

	const PortId id = addPortOfType(dyn, "int", "x");
	REQUIRE(id != PortId{});
	REQUIRE(dyn.inputCount() == 1);
	REQUIRE(dyn.findInput(id)->type() == std::type_index(typeid(int)));

	// An unregistered key adds nothing.
	REQUIRE(addPortOfType(dyn, "nope", "y") == PortId{});
	REQUIRE(dyn.inputCount() == 1);
}

TEST_CASE("edit::addPort routes through the registry on the mutation seam", "[flow][dynamic]")
{
	registerPortType<int>("int");
	Graph g;
	const NodeId n = g.add<DynInts>();
	const NodeId fixed = g.add<test::ConstInt>(1); // not a DynamicPortsNode

	REQUIRE(edit::addPort(g, n, "int", "x") != PortId{});
	REQUIRE(g.node(n).inputCount() == 1);
	REQUIRE(edit::addPort(g, fixed, "int", "x") == PortId{}); // fixed node -> refused
}

TEST_CASE("setName renames a pin without disturbing its edge", "[flow][dynamic]")
{
	Graph g;
	const NodeId c = g.add<test::ConstInt>(5);
	const NodeId n = g.add<DynInts>();
	auto& dyn = static_cast<DynInts&>(g.node(n));
	const PortId pin = dyn.addDynamicPort<int>("in0");
	REQUIRE(g.connect(c, 0, n, 0) == Connection::Ok);

	dyn.findInput(pin)->setName("renamed");
	REQUIRE(dyn.findInput(pin)->name() == "renamed");
	REQUIRE(g.edges().size() == 1); // the edge (addressed by PortId) is untouched
	REQUIRE(g.edges().front().to == PortAddress{n, pin});
}

TEST_CASE("acceptsPortType narrows the addable types", "[flow][dynamic]")
{
	struct ImagesOnly : DynamicPortsNode
	{
		ImagesOnly()
			: DynamicPortsNode("ImagesOnly")
		{
		}
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }
		bool acceptsPortType(const std::string& key) const override { return key == "Image"; }
		void compute() override {}
	};

	ImagesOnly node;
	REQUIRE(node.acceptsPortType("Image"));
	REQUIRE_FALSE(node.acceptsPortType("int"));
}
