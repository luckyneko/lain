// Unit tests for Port / Node / Graph — the graph data model: port declaration,
// type-checked + cycle-rejecting connect, disconnect, topo order, and the
// compute() read/write path (driven directly here; the scheduler is step 4).

#include "lain/flow/graph.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <typeindex>

using namespace lain::flow;

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

	REQUIRE(g.connect(NodeId{99}, 0, add, 0) == Connection::InvalidNode);
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
	REQUIRE(order.size() == 3);
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

	REQUIRE(g.nodeCount() == 1);
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
	REQUIRE(g.nodeCount() == 2);
	REQUIRE(g.edges().size() == 1);		   // the c1 -> add edge went with it
	REQUIRE(g.edges().front().from == c2); // the c2 -> add edge survives

	// The surviving nodes keep their ids, and topo order no longer mentions c1.
	REQUIRE(g.node(c2).name() == "ConstInt");
	REQUIRE(g.node(add).name() == "Add");
	const std::vector<NodeId>& order = g.topoOrder();
	REQUIRE(order.size() == 2);
	REQUIRE(std::find(order.begin(), order.end(), c1) == order.end());

	// The freed input can take a new source; removing an absent node is a no-op.
	REQUIRE(g.connect(c2, 0, add, 0) == Connection::Ok);
	REQUIRE_FALSE(g.removeNode(c1));
}
