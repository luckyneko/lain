// Tests for the graph I/O boundary seam (boundary.h): a GroupInputNode publishes host-set
// values into the graph, a GroupOutputNode exposes delivered values, and Graph flattens
// every boundary node's pins into one bindable list. Driver-free — the payload is int, so
// the seam is proven without any GPU/image type (it is payload-agnostic by construction).
// Multi-pin is exercised directly; a single-pin graph is the vertical-a case.

#include "lain/flow/boundary.h"
#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h" // AddInt — a compute node to route a boundary value through

#include <catch2/catch_test_macros.hpp>

#include <typeindex>

using namespace lain::flow;

TEST_CASE("a host-set input pin crosses to an output pin through a run", "[flow][boundary]")
{
	Graph g;
	const NodeId in = g.add<GroupInputNode>();
	const NodeId out = g.add<GroupOutputNode>();
	auto& gin = static_cast<GroupInputNode&>(g.node(in));
	auto& gout = static_cast<GroupOutputNode&>(g.node(out));
	const PortId src = gin.addBoundary<int>("source");
	const PortId res = gout.addBoundary<int>("result");
	REQUIRE(g.connect(in, 0, out, 0) == Connection::Ok); // first output -> first input

	// The generic host path: build a type-erased PortValue and inject it into the pin.
	PortValue v;
	v.set<int>(42);
	gin.setValue(src, std::move(v));

	SerialScheduler().run(g);

	REQUIRE(gout.value(res).holds<int>());
	REQUIRE(gout.value(res).get<int>() == 42);
}

TEST_CASE("one node carries several independently-typed, independently-bound pins", "[flow][boundary]")
{
	// A single GroupInputNode with two pins of different types, each bound + read back — the
	// point of the multi-port shape (pins differ in type with no dynamic/runtime-typed ports).
	Graph g;
	const NodeId in = g.add<GroupInputNode>();
	const NodeId out = g.add<GroupOutputNode>();
	auto& gin = static_cast<GroupInputNode&>(g.node(in));
	auto& gout = static_cast<GroupOutputNode&>(g.node(out));
	const PortId pi = gin.addBoundary<int>("count");
	const PortId pf = gin.addBoundary<float>("scale");
	const PortId oi = gout.addBoundary<int>("count");
	const PortId of = gout.addBoundary<float>("scale");
	REQUIRE(g.connect(in, 0, out, 0) == Connection::Ok); // int pins (index 0)
	REQUIRE(g.connect(in, 1, out, 1) == Connection::Ok); // float pins (index 1)

	PortValue vi;
	vi.set<int>(3);
	PortValue vf;
	vf.set<float>(1.5f);
	gin.setValue(pi, std::move(vi));
	gin.setValue(pf, std::move(vf));

	SerialScheduler().run(g);
	REQUIRE(gout.value(oi).get<int>() == 3);
	REQUIRE(gout.value(of).get<float>() == 1.5f);
}

TEST_CASE("a boundary value flows through an intermediate compute node", "[flow][boundary]")
{
	// GroupInput -> AddInt(+ a constant) -> GroupOutput: the value drives real computation,
	// not a straight passthrough.
	Graph g;
	const NodeId in = g.add<GroupInputNode>();
	const NodeId k = g.add<test::ConstInt>(100);
	const NodeId add = g.add<test::AddInt>();
	const NodeId out = g.add<GroupOutputNode>();
	auto& gin = static_cast<GroupInputNode&>(g.node(in));
	auto& gout = static_cast<GroupOutputNode&>(g.node(out));
	const PortId src = gin.addBoundary<int>("source");
	const PortId res = gout.addBoundary<int>("result");
	REQUIRE(g.connect(in, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(k, 0, add, 1) == Connection::Ok);
	REQUIRE(g.connect(add, 0, out, 0) == Connection::Ok);

	PortValue v;
	v.set<int>(5);
	gin.setValue(src, std::move(v));

	SerialScheduler().run(g);
	REQUIRE(gout.value(res).get<int>() == 105);
}

TEST_CASE("re-binding an input pin refires it on the next run", "[flow][boundary]")
{
	Graph g;
	const NodeId in = g.add<GroupInputNode>();
	const NodeId out = g.add<GroupOutputNode>();
	auto& gin = static_cast<GroupInputNode&>(g.node(in));
	auto& gout = static_cast<GroupOutputNode&>(g.node(out));
	const PortId src = gin.addBoundary<int>("source");
	const PortId res = gout.addBoundary<int>("result");
	REQUIRE(g.connect(in, 0, out, 0) == Connection::Ok);

	PortValue a;
	a.set<int>(1);
	gin.setValue(src, std::move(a));
	SerialScheduler().run(g);
	REQUIRE(gout.value(res).get<int>() == 1);

	PortValue b;
	b.set<int>(7);
	gin.setValue(src, std::move(b)); // marks dirty -> republished
	SerialScheduler().run(g);
	REQUIRE(gout.value(res).get<int>() == 7);
}

TEST_CASE("an unbound input pin delivers an empty value", "[flow][boundary]")
{
	Graph g;
	const NodeId in = g.add<GroupInputNode>();
	const NodeId out = g.add<GroupOutputNode>();
	auto& gin = static_cast<GroupInputNode&>(g.node(in));
	auto& gout = static_cast<GroupOutputNode&>(g.node(out));
	gin.addBoundary<int>("source"); // added but never bound
	const PortId res = gout.addBoundary<int>("result");
	REQUIRE(g.connect(in, 0, out, 0) == Connection::Ok);

	SerialScheduler().run(g); // never setValue'd
	REQUIRE(gout.value(res).empty());
}

TEST_CASE("Graph flattens boundary pins with name and type", "[flow][boundary]")
{
	Graph g;
	const NodeId in = g.add<GroupInputNode>();
	g.add<test::ConstInt>(0); // an ordinary node — not a boundary, must be excluded
	const NodeId out = g.add<GroupOutputNode>();
	auto& gin = static_cast<GroupInputNode&>(g.node(in));
	auto& gout = static_cast<GroupOutputNode&>(g.node(out));
	gin.addBoundary<int>("count");
	gin.addBoundary<float>("scale");
	gout.addBoundary<int>("result");

	const auto inputs = g.boundaryInputs();
	const auto outputs = g.boundaryOutputs();
	REQUIRE(inputs.size() == 2); // both pins of the one input node, flattened
	REQUIRE(outputs.size() == 1);
	REQUIRE(inputs[0].name() == "count");
	REQUIRE(inputs[0].type() == std::type_index(typeid(int)));
	REQUIRE(inputs[1].name() == "scale");
	REQUIRE(inputs[1].type() == std::type_index(typeid(float)));
	REQUIRE(outputs[0].name() == "result");

	// The handle binds without touching the node directly.
	PortValue v;
	v.set<int>(9);
	inputs[0].setValue(std::move(v));
	REQUIRE(static_cast<GroupInputNode&>(g.node(in)).boundaryCount() == 2);
}
