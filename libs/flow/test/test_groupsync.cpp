// Tests for edit::syncGroupPorts — reconciling a group node's own ports against its inner graph's
// boundary pins. A group's interface IS that boundary, so every way the boundary can move (a pin
// added, renamed, retyped, removed) has to land correctly on the outer ports — and, crucially, on
// the parent's edges into them.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lain::flow;

namespace
{
	// A group with a pass-through inner graph: GroupInput -> GroupOutput directly, so whatever pins
	// exist on the boundary define the whole interface.
	NodeId addPassGroup(Graph& parent)
	{
		return parent.add<GroupNode>();
	}

	GroupNode& groupAt(Graph& g, NodeId id)
	{
		return static_cast<GroupNode&>(g.node(id));
	}

	const Port* findPortNamed(const Node& node, Port::Direction dir, const std::string& name)
	{
		const std::size_t count = (dir == Port::Direction::Input) ? node.inputCount() : node.outputCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			const Port& p = (dir == Port::Direction::Input) ? node.input(i) : node.output(i);
			if (p.name() == name)
				return &p;
		}
		return nullptr;
	}
} // namespace

TEST_CASE("sync mirrors the inner boundary pins onto the group's own ports", "[flow][group][edit]")
{
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);

	REQUIRE(g.node(id).inputCount() == 0); // a fresh group exposes nothing
	REQUIRE(g.node(id).outputCount() == 0);

	const PortId inPin = group.inner().boundaryInputNode().addBoundary<int>("count");
	const PortId outPin = group.inner().boundaryOutputNode().addBoundary<float>("scale");

	const edit::GroupSync sync = edit::syncGroupPorts(g, id);
	REQUIRE(sync.added == 2);
	REQUIRE(sync.removed == 0);
	REQUIRE(sync.disconnected == 0);

	REQUIRE(g.node(id).inputCount() == 1);
	REQUIRE(g.node(id).outputCount() == 1);

	// Name AND type are mirrored — the type with no compile-time T, straight off the pin's flyweight.
	const Port& outerIn = g.node(id).input(0);
	const Port& outerOut = g.node(id).output(0);
	REQUIRE(outerIn.name() == "count");
	REQUIRE(outerIn.type() == std::type_index(typeid(int)));
	REQUIRE(outerOut.name() == "scale");
	REQUIRE(outerOut.type() == std::type_index(typeid(float)));

	// And the mapping records which inner pin each mirrors.
	REQUIRE(group.innerPin(outerIn.id()) == inPin);
	REQUIRE(group.innerPin(outerOut.id()) == outPin);

	SECTION("syncing again changes nothing — it is idempotent")
	{
		const edit::GroupSync again = edit::syncGroupPorts(g, id);
		REQUIRE_FALSE(again.changed());
		REQUIRE(g.node(id).inputCount() == 1);
		REQUIRE(g.node(id).outputCount() == 1);
	}
}

TEST_CASE("an inner pin rename retitles the outer port and KEEPS its wiring", "[flow][group][edit]")
{
	// The point of mapping by PortId: a rename is display-only, so the parent's edge survives.
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);
	const PortId inPin = group.inner().boundaryInputNode().addBoundary<int>("source");
	edit::syncGroupPorts(g, id);

	const PortId outerId = g.node(id).input(0).id();
	const PortId x = g.boundaryInputNode().addBoundary<int>("x");
	REQUIRE(g.connect({g.boundaryInputNode().id(), x}, {id, outerId}) == Connection::Ok);

	group.inner().boundaryInputNode().findOutput(inPin)->setName("renamed");
	const edit::GroupSync sync = edit::syncGroupPorts(g, id);

	REQUIRE(sync.renamed == 1);
	REQUIRE(sync.removed == 0);
	REQUIRE(sync.disconnected == 0); // nothing was cut
	REQUIRE(g.node(id).input(0).name() == "renamed");
	REQUIRE(g.node(id).input(0).id() == outerId); // same port, same identity
	REQUIRE(g.edges().size() == 1);				  // and the parent's edge is still there
}

TEST_CASE("a removed inner pin drops the outer port and reports the edges it cut", "[flow][group][edit]")
{
	// The reason this is a gesture and not a node method: the outer port may be wired in the PARENT,
	// and only the parent Graph can cut those edges (Graph::removePort refuses a connected pin).
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);
	const PortId outPin = group.inner().boundaryOutputNode().addBoundary<int>("result");
	edit::syncGroupPorts(g, id);

	// Fan the group's output out to two consumers, so the report has to count both.
	const PortId y = g.boundaryOutputNode().addBoundary<int>("y");
	const NodeId sink = g.add<test::AddInt>();
	const PortAddress source{id, g.node(id).output(0).id()};
	REQUIRE(g.connect(source, {g.boundaryOutputNode().id(), y}) == Connection::Ok);
	REQUIRE(g.connect(source, {sink, g.node(sink).input(0).id()}) == Connection::Ok);
	REQUIRE(g.edges().size() == 2);

	// Drop the pin from the inner interface.
	REQUIRE(group.inner().removePort({group.inner().boundaryOutputNode().id(), outPin}));

	const edit::GroupSync sync = edit::syncGroupPorts(g, id);
	REQUIRE(sync.removed == 1);
	REQUIRE(sync.disconnected == 2); // both consumers reported — a host can tell the user
	REQUIRE(g.node(id).outputCount() == 0);
	REQUIRE(g.edges().empty());
	REQUIRE(group.innerPin(source.port) == PortId{}); // mapping cleaned up too
}

TEST_CASE("sync survives a pin being replaced by one of the same name", "[flow][group][edit]")
{
	// Remove-then-add with the SAME name: identity is the id, so this must come out as one port
	// removed and one added, not a rename — and the ordering (remove before add) is what keeps the
	// new port from colliding with the old one's name.
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);
	GroupInputNode& boundary = group.inner().boundaryInputNode();

	const PortId first = boundary.addBoundary<int>("value");
	edit::syncGroupPorts(g, id);
	const PortId firstOuter = g.node(id).input(0).id();

	REQUIRE(group.inner().removePort({boundary.id(), first}));
	const PortId second = boundary.addBoundary<float>("value"); // same name, new type

	const edit::GroupSync sync = edit::syncGroupPorts(g, id);
	REQUIRE(sync.removed == 1);
	REQUIRE(sync.added == 1);
	REQUIRE(g.node(id).inputCount() == 1);

	const Port& outer = g.node(id).input(0);
	REQUIRE(outer.name() == "value");
	REQUIRE(outer.type() == std::type_index(typeid(float))); // the NEW pin's type
	REQUIRE(outer.id() != firstOuter);						 // a different port, not the old one retyped
	REQUIRE(group.innerPin(outer.id()) == second);
}

TEST_CASE("a synced group runs end to end", "[flow][group][edit][scheduler]")
{
	// Sync is what makes a group usable without hand-wiring exposePort: build the inner graph,
	// sync, connect, run.
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);
	Graph& inner = group.inner();

	const PortId inPin = inner.boundaryInputNode().addBoundary<int>("in");
	const PortId outPin = inner.boundaryOutputNode().addBoundary<int>("out");
	const NodeId konst = inner.add<test::ConstInt>(100);
	const NodeId add = inner.add<test::AddInt>();
	REQUIRE(inner.connect({inner.boundaryInputNode().id(), inPin}, {add, inner.node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(inner.connect(konst, 0, add, 1) == Connection::Ok);
	REQUIRE(inner.connect({add, inner.node(add).output(0).id()}, {inner.boundaryOutputNode().id(), outPin}) == Connection::Ok);

	REQUIRE(edit::syncGroupPorts(g, id).added == 2);

	const PortId x = g.boundaryInputNode().addBoundary<int>("x");
	const PortId y = g.boundaryOutputNode().addBoundary<int>("y");
	REQUIRE(g.connect({g.boundaryInputNode().id(), x}, {id, g.node(id).input(0).id()}) == Connection::Ok);
	REQUIRE(g.connect({id, g.node(id).output(0).id()}, {g.boundaryOutputNode().id(), y}) == Connection::Ok);

	Evaluation e{g};
	PortValue v;
	v.set<int>(5);
	e.bind(PortAddress{g.boundaryInputNode().id(), x}, std::move(v));
	SerialScheduler().run(g, e);

	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 105);
}

TEST_CASE("sync is a no-op on a node that contains no graph", "[flow][group][edit]")
{
	Graph g;
	const NodeId plain = g.add<test::ConstInt>(1);
	const edit::GroupSync sync = edit::syncGroupPorts(g, plain);
	REQUIRE_FALSE(sync.changed());
	REQUIRE(g.node(plain).outputCount() == 1); // its own port is untouched

	// And an id that isn't in the graph at all.
	REQUIRE_FALSE(edit::syncGroupPorts(g, NodeId{}).changed());
}

TEST_CASE("sync mirrors a type no port-type registry knows", "[flow][group][edit]")
{
	// A group's ports are DERIVED, not user-chosen, so they must not be limited to the registered
	// set the way an addable dynamic pin is. This type is registered nowhere.
	struct Unregistered
	{
		int payload = 0;
	};

	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);
	group.inner().boundaryInputNode().addBoundary<Unregistered>("odd");

	REQUIRE(edit::syncGroupPorts(g, id).added == 1);
	REQUIRE(findPortNamed(g.node(id), Port::Direction::Input, "odd") != nullptr);
	REQUIRE(g.node(id).input(0).type() == std::type_index(typeid(Unregistered)));
}

TEST_CASE("inner pins with COLLIDING ids on opposite sides never cross", "[flow][group][edit][scheduler]")
{
	// A PortId is minted per NODE, so the inner GroupInput's first pin and the inner GroupOutput's
	// first pin are BOTH PortId{1}. The outer->inner map is keyed by the outer port's id and
	// resolved using that port's DIRECTION, which is what keeps the two apart. Wire the inner graph
	// crossed (a -> q, b -> p) so any mix-up shows up as swapped values rather than passing by luck.
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);
	Graph& inner = group.inner();
	GroupInputNode& bIn = inner.boundaryInputNode();
	GroupOutputNode& bOut = inner.boundaryOutputNode();

	const PortId a = bIn.addBoundary<int>("a");
	const PortId b = bIn.addBoundary<int>("b");
	const PortId p = bOut.addBoundary<int>("p");
	const PortId q = bOut.addBoundary<int>("q");
	REQUIRE(a == p); // the collision this test is about: same id value, different nodes
	REQUIRE(b == q);

	REQUIRE(inner.connect({bIn.id(), a}, {bOut.id(), q}) == Connection::Ok); // crossed on purpose
	REQUIRE(inner.connect({bIn.id(), b}, {bOut.id(), p}) == Connection::Ok);

	REQUIRE(edit::syncGroupPorts(g, id).added == 4);

	const PortId x1 = g.boundaryInputNode().addBoundary<int>("x1");
	const PortId x2 = g.boundaryInputNode().addBoundary<int>("x2");
	const PortId yp = g.boundaryOutputNode().addBoundary<int>("yp");
	const PortId yq = g.boundaryOutputNode().addBoundary<int>("yq");
	const NodeId root = g.boundaryInputNode().id();
	const NodeId sink = g.boundaryOutputNode().id();

	REQUIRE(g.connect({root, x1}, {id, findPortNamed(g.node(id), Port::Direction::Input, "a")->id()}) == Connection::Ok);
	REQUIRE(g.connect({root, x2}, {id, findPortNamed(g.node(id), Port::Direction::Input, "b")->id()}) == Connection::Ok);
	REQUIRE(g.connect({id, findPortNamed(g.node(id), Port::Direction::Output, "p")->id()}, {sink, yp}) == Connection::Ok);
	REQUIRE(g.connect({id, findPortNamed(g.node(id), Port::Direction::Output, "q")->id()}, {sink, yq}) == Connection::Ok);

	Evaluation e{g};
	PortValue v1;
	v1.set<int>(11);
	PortValue v2;
	v2.set<int>(22);
	e.bind(PortAddress{g.boundaryInputNode().id(), x1}, std::move(v1));
	e.bind(PortAddress{g.boundaryInputNode().id(), x2}, std::move(v2));

	SerialScheduler().run(g, e);

	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), yp}).get<int>() == 22); // b (22) -> p
	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), yq}).get<int>() == 11); // a (11) -> q
}

TEST_CASE("pins added one at a time, syncing between, all reach the outer node", "[flow][group][edit]")
{
	// The INTERACTIVE ordering, which is what the host actually produces: it syncs every frame, so a
	// sync lands BETWEEN each pin the user adds. The earlier tests all added their pins first and
	// synced once, which hid a real bug — with a single shared "already mirrored" set, the input's
	// inner PortId{1} masked the output's inner PortId{1} (ids are minted per NODE, so both boundary
	// nodes start at 1) and the output port silently never appeared.
	Graph g;
	const NodeId id = addPassGroup(g);
	GroupNode& group = groupAt(g, id);

	// 1. Add an input pin, then sync (as a frame would).
	group.inner().boundaryInputNode().addBoundary<int>("in");
	REQUIRE(edit::syncGroupPorts(g, id).added == 1);
	REQUIRE(g.node(id).inputCount() == 1);
	REQUIRE(g.node(id).outputCount() == 0);

	// 2. Add an output pin, then sync again. Its inner id collides with the input pin's.
	const PortId outPin = group.inner().boundaryOutputNode().addBoundary<int>("out");
	REQUIRE(outPin == group.inner().boundaryInputNode().output(0).id()); // the collision, made explicit

	REQUIRE(edit::syncGroupPorts(g, id).added == 1);
	REQUIRE(g.node(id).inputCount() == 1);
	REQUIRE(g.node(id).outputCount() == 1); // the port that used to go missing

	SECTION("and the group then actually carries a value through both ports")
	{
		// The user's next move: wire the group up and expect data to cross. Inside, pass the input
		// straight to the output; outside, feed it from the root boundary and read the result.
		Graph& inner = group.inner();
		const NodeId bIn = inner.boundaryInputNode().id();
		const NodeId bOut = inner.boundaryOutputNode().id();
		REQUIRE(inner.connect({bIn, inner.boundaryInputNode().output(0).id()},
							  {bOut, inner.boundaryOutputNode().input(0).id()}) == Connection::Ok);

		const PortId x = g.boundaryInputNode().addBoundary<int>("x");
		const PortId y = g.boundaryOutputNode().addBoundary<int>("y");
		REQUIRE(g.connect({g.boundaryInputNode().id(), x}, {id, g.node(id).input(0).id()}) == Connection::Ok);
		REQUIRE(g.connect({id, g.node(id).output(0).id()}, {g.boundaryOutputNode().id(), y}) == Connection::Ok);

		Evaluation e{g};
		PortValue v;
		v.set<int>(42);
		e.bind(PortAddress{g.boundaryInputNode().id(), x}, std::move(v));
		SerialScheduler().run(g, e);

		REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).holds<int>());
		REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 42);
	}

	SECTION("and it keeps working as more pins arrive on both sides")
	{
		group.inner().boundaryInputNode().addBoundary<float>("in2");
		REQUIRE(edit::syncGroupPorts(g, id).added == 1);
		group.inner().boundaryOutputNode().addBoundary<float>("out2");
		REQUIRE(edit::syncGroupPorts(g, id).added == 1);

		REQUIRE(g.node(id).inputCount() == 2);
		REQUIRE(g.node(id).outputCount() == 2);
		REQUIRE_FALSE(edit::syncGroupPorts(g, id).changed()); // and it settles
	}
}
