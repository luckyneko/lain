// Unit tests for Port / Node / Graph — the graph data model: port declaration,
// type-checked + cycle-rejecting connect, disconnect, topo order, and the
// compute() read/write path (driven directly here; the scheduler is step 4).

#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <typeindex>

using namespace lain::flow;

// Every Graph is born with its boundary pair (one GroupInput + one GroupOutput), so nodeCount()
// is that many more than the nodes a test added itself.
static constexpr std::size_t kBoundaryNodes = 2;

namespace
{
	// A constant source: one int output, set on compute.
	struct ConstInt : Node
	{
		int value;
		PortIndex out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		void compute() override { output(out).set(value); }
	};

	// Two int inputs -> their sum.
	struct AddInt : Node
	{
		PortIndex a, b, sum;
		AddInt()
			: Node("Add")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			sum = addOutput<int>("sum");
		}
		void compute() override { output(sum).set(input(a).get<int>() + input(b).get<int>()); }
	};

	// One float input — used to provoke a type mismatch against an int output.
	struct SinkFloat : Node
	{
		PortIndex in;
		SinkFloat()
			: Node("Sink")
		{
			in = addInput<float>("x");
		}
		void compute() override {}
	};

	std::ptrdiff_t indexOf(const std::vector<NodeId>& order, NodeId id)
	{
		return std::find(order.begin(), order.end(), id) - order.begin();
	}
} // namespace

TEST_CASE("a node exposes its declared ports", "[graph]")
{
	Graph g;
	const NodeId add = g.add<AddInt>();
	const Node& n = g.node(add);

	REQUIRE(n.name() == "Add");
	REQUIRE(n.inputCount() == 2);
	REQUIRE(n.outputCount() == 1);
	REQUIRE(n.input(0).name() == "a");
	REQUIRE(n.input(0).type() == std::type_index(typeid(int)));
	REQUIRE(std::string(n.input(0).typeName()) == "int"); // captured from lain::meta::typeName<int>()
	REQUIRE(n.output(0).name() == "sum");
	REQUIRE(n.id() == add);
	REQUIRE(n.dirty()); // freshly added nodes are dirty
}

TEST_CASE("connect type-checks declared port types", "[graph]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);
	const NodeId add = g.add<AddInt>();
	const NodeId sink = g.add<SinkFloat>();

	REQUIRE(g.connect(c, 0, add, 0) == Connection::Ok);			   // int -> int
	REQUIRE(g.connect(c, 0, sink, 0) == Connection::TypeMismatch); // int -> float
	REQUIRE(g.edges().size() == 1);
}

TEST_CASE("connect rejects invalid nodes and ports", "[graph]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);
	const NodeId add = g.add<AddInt>();

	REQUIRE(g.connect(NodeId::generate(), 0, add, 0) == Connection::InvalidNode); // an id no node here holds
	REQUIRE(g.connect(c, 5, add, 0) == Connection::InvalidPort);
	REQUIRE(g.connect(c, 0, add, 9) == Connection::InvalidPort);
}

TEST_CASE("an input takes a single source until disconnected", "[graph]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(1);
	const NodeId c2 = g.add<ConstInt>(2);
	const NodeId add = g.add<AddInt>();

	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, add, 0) == Connection::InputInUse);

	REQUIRE(g.disconnect(add, 0));
	REQUIRE_FALSE(g.disconnect(add, 0)); // nothing left to remove
	REQUIRE(g.connect(c2, 0, add, 0) == Connection::Ok);
}

TEST_CASE("an output fans out to many inputs (only inputs are single-source)", "[graph]")
{
	// The single-source rule guards inputs, not outputs: one output may feed several inputs,
	// each copying the (persistent) value. Here one source drives both inputs of an adder
	// (9 + 9 = 18) — proving the output carries two edges. This locks in the gui fan-out.
	Graph g;
	const NodeId c = g.add<ConstInt>(9);
	const NodeId add = g.add<AddInt>();

	REQUIRE(g.connect(c, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c, 0, add, 1) == Connection::Ok); // same output, second consumer: fine
	REQUIRE(g.edges().size() == 2);

	SerialScheduler().run(g);
	REQUIRE(g.node(add).output(0).get<int>() == 18);
}

TEST_CASE("connect rejects cycles", "[graph]")
{
	Graph g;
	const NodeId a1 = g.add<AddInt>();
	const NodeId a2 = g.add<AddInt>();

	REQUIRE(g.connect(a1, 0, a2, 0) == Connection::Ok);
	REQUIRE(g.connect(a2, 0, a1, 0) == Connection::WouldCycle); // back-edge
	REQUIRE(g.connect(a1, 0, a1, 1) == Connection::WouldCycle); // self-edge
}

TEST_CASE("topoOrder places sources before dependents", "[graph]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(2);
	const NodeId c2 = g.add<ConstInt>(3);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, add, 1) == Connection::Ok);

	const std::vector<NodeId>& order = g.topoOrder();
	REQUIRE(order.size() == kBoundaryNodes + 3); // topo covers every node, the boundary pair included
	REQUIRE(indexOf(order, add) > indexOf(order, c1));
	REQUIRE(indexOf(order, add) > indexOf(order, c2));
}

TEST_CASE("compute reads inputs and writes outputs", "[graph]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(42);

	REQUIRE_FALSE(g.node(c).output(0).ready());
	g.node(c).compute();
	REQUIRE(g.node(c).output(0).ready());
	REQUIRE(g.node(c).output(0).get<int>() == 42);

	const NodeId add = g.add<AddInt>();
	g.node(add).input(0).set(2); // the scheduler will populate these from edges; here we drive directly
	g.node(add).input(1).set(40);
	g.node(add).compute();
	REQUIRE(g.node(add).output(0).get<int>() == 42);
}

TEST_CASE("dirty flag toggles", "[graph]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);

	REQUIRE(g.node(c).dirty());
	g.node(c).clearDirty();
	REQUIRE_FALSE(g.node(c).dirty());
	g.node(c).markDirty();
	REQUIRE(g.node(c).dirty());
}

TEST_CASE("add adopts an already-constructed node", "[graph]")
{
	Graph g;
	const NodeId id = g.add(std::make_unique<AddInt>()); // the factory path

	REQUIRE(g.nodeCount() == kBoundaryNodes + 1);
	REQUIRE(g.node(id).name() == "Add");
	REQUIRE(g.node(id).id() == id); // the graph stamped the id on the adopted node
}

TEST_CASE("removeNode drops the node and its edges, leaving other ids valid", "[graph]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(1);
	const NodeId c2 = g.add<ConstInt>(2);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, add, 1) == Connection::Ok);

	REQUIRE(g.removeNode(c1)); // remove a source
	REQUIRE(g.nodeCount() == kBoundaryNodes + 2);
	REQUIRE(g.edges().size() == 1);				// the c1 -> add edge went with it
	REQUIRE(g.edges().front().from.node == c2); // the c2 -> add edge survives

	// The surviving nodes keep their ids, and topo order no longer mentions c1.
	REQUIRE(g.node(c2).name() == "ConstInt");
	REQUIRE(g.node(add).name() == "Add");
	const std::vector<NodeId>& order = g.topoOrder();
	REQUIRE(order.size() == kBoundaryNodes + 2);
	REQUIRE(std::find(order.begin(), order.end(), c1) == order.end());

	// The freed input can take a new source; removing an absent node is a no-op.
	REQUIRE(g.connect(c2, 0, add, 0) == Connection::Ok);
	REQUIRE_FALSE(g.removeNode(c1));
}

// --- identity (ADR-0011) ----------------------------------------------------

TEST_CASE("every added node gets a fresh, non-null identity", "[graph][identity]")
{
	Graph g;
	const NodeId a = g.add<ConstInt>(1);
	const NodeId b = g.add<ConstInt>(2);

	REQUIRE(a != NodeId{}); // the null NodeId is the "no node" sentinel, never minted
	REQUIRE(b != NodeId{});
	REQUIRE(a != b);
	// Ids are unique across GRAPHS too — that is the whole point of a uuid over a per-graph
	// counter, since a group node makes every group own a Graph.
	Graph other;
	REQUIRE(other.add<ConstInt>(1) != a);
	REQUIRE(other.boundaryInputNode().id() != g.boundaryInputNode().id());
}

TEST_CASE("nodeIds is insertion order, independent of id order", "[graph][identity]")
{
	Graph g;
	const NodeId a = g.add<ConstInt>(1);
	const NodeId b = g.add<ConstInt>(2);
	const NodeId c = g.add<ConstInt>(3);

	// The boundary pair is inserted first (it is a construction invariant), then the three adds in
	// the order they were made. A uuid comparison would say something arbitrary about a/b/c.
	const std::vector<NodeId> expected{g.boundaryInputNode().id(), g.boundaryOutputNode().id(), a, b, c};
	REQUIRE(g.nodeIds() == expected);

	REQUIRE(g.removeNode(b)); // removal closes the gap, leaving the rest in order
	REQUIRE(g.nodeIds() == std::vector<NodeId>{g.boundaryInputNode().id(), g.boundaryOutputNode().id(), a, c});
}

TEST_CASE("topoOrder is deterministic across rebuilds of the same graph", "[graph][identity]")
{
	// Seeded from insertion order rather than from the id-keyed map — otherwise two builds of the
	// same graph would order their edgeless nodes differently, and topo order drives serial
	// execution and the canvas's default columns.
	const auto build = [](Graph& g)
	{
		const NodeId c1 = g.add<ConstInt>(2);
		const NodeId c2 = g.add<ConstInt>(3);
		const NodeId add = g.add<AddInt>();
		REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
		REQUIRE(g.connect(c2, 0, add, 1) == Connection::Ok);
	};

	Graph first;
	Graph second;
	build(first);
	build(second);

	// Compare by POSITION-IN-INSERTION-ORDER: the two graphs' ids differ (each mints its own), so
	// what must match is the shape of the order, not the values.
	const auto shape = [](const Graph& g)
	{
		std::vector<std::ptrdiff_t> positions;
		for (const NodeId id : g.topoOrder())
			positions.push_back(indexOf(g.nodeIds(), id));
		return positions;
	};
	REQUIRE(shape(first) == shape(second));
}

TEST_CASE("a requested identity is restored, a duplicate is re-minted", "[graph][identity]")
{
	Graph g;
	const NodeId wanted = NodeId::generate();

	// The loader path: a saved node comes back AS ITSELF.
	const NodeId restored = g.add(std::make_unique<ConstInt>(1), wanted);
	REQUIRE(restored == wanted);
	REQUIRE(g.node(wanted).name() == "ConstInt");

	// Asking for it a second time never overwrites the owner: the newcomer is re-minted, and the
	// caller sees that by comparing the returned id with the one it asked for.
	const NodeId clash = g.add(std::make_unique<ConstInt>(2), wanted);
	REQUIRE(clash != NodeId{});
	REQUIRE(clash != wanted);
	g.node(wanted).compute();
	REQUIRE(g.node(wanted).output(0).get<int>() == 1); // still the FIRST node, not the newcomer
	REQUIRE(g.nodeCount() == kBoundaryNodes + 2);

	// A null request mints, exactly as the plain add() overload does.
	const NodeId minted = g.add(std::make_unique<ConstInt>(3), NodeId{});
	REQUIRE(minted != NodeId{});
}

TEST_CASE("the boundary pair can be born with saved ids", "[graph][identity]")
{
	const NodeId in = NodeId::generate();
	const NodeId out = NodeId::generate();
	Graph g{BoundaryIds{in, out}};

	REQUIRE(g.boundaryInputNode().id() == in);
	REQUIRE(g.boundaryOutputNode().id() == out);
	REQUIRE(g.nodeIds() == std::vector<NodeId>{in, out}); // still first, still in that order

	// Construction is TOTAL: a document that names neither boundary node, or names one twice,
	// still opens — the missing/clashing id is minted.
	Graph none{BoundaryIds{}};
	REQUIRE(none.boundaryInputNode().id() != NodeId{});
	REQUIRE(none.boundaryOutputNode().id() != NodeId{});
	REQUIRE(none.boundaryInputNode().id() != none.boundaryOutputNode().id());

	Graph doubled{BoundaryIds{in, in}};
	REQUIRE(doubled.boundaryInputNode().id() == in);
	REQUIRE(doubled.boundaryOutputNode().id() != in);
}

TEST_CASE("add still refuses a second boundary node, whatever id is requested", "[graph][identity]")
{
	Graph g;
	REQUIRE(g.add(std::make_unique<GroupInputNode>(), NodeId::generate()) == NodeId{});
	REQUIRE(g.add(std::make_unique<GroupOutputNode>(), NodeId::generate()) == NodeId{});
	REQUIRE(g.nodeCount() == kBoundaryNodes);
	REQUIRE(g.nodeIds().size() == kBoundaryNodes); // the refusal leaves the order vector alone too
}
