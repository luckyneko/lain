// The generic control nodes (WORK.md Tier A #2, slice 2): Constant, Gate, Select, Merge, wired into
// the readiness mechanism (ADR-0007). A Gate suppresses downstream when off; a Merge/Select forwards
// a live branch past a gated-off (empty) one.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/nodes/constant.h"
#include "lain/flow/nodes/gate.h"
#include "lain/flow/nodes/merge.h"
#include "lain/flow/nodes/select.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <catch2/catch_test_macros.hpp>

using lain::flow::Connection;
using lain::flow::ConstantNode;
using lain::flow::constantOf;
using lain::flow::Evaluation;
using lain::flow::GateNode;
using lain::flow::Graph;
using lain::flow::MergeNode;
using lain::flow::Node;
using lain::flow::NodeId;
using lain::flow::Param;
using lain::flow::portType;
using lain::flow::SelectNode;
using lain::flow::SerialScheduler;

TEST_CASE("Constant emits its value and re-emits on change", "[flow][nodes]")
{
	Graph graph;
	const NodeId c = graph.add(constantOf(42));

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{c, graph.node(c).output(0).id()}).get<int>() == 42);

	static_cast<ConstantNode&>(graph.node(c)).setValue(7);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{c, graph.node(c).output(0).id()}).get<int>() == 7);
}

TEST_CASE("Gate passes its value when enabled and suppresses when not", "[flow][nodes]")
{
	Graph graph;
	const NodeId enable = graph.add(constantOf(true));
	const NodeId value = graph.add(constantOf(5));
	const NodeId gate = graph.add<GateNode>(portType<int>());
	graph.connect(enable, 0, gate, 0); // -> enable
	graph.connect(value, 0, gate, 1);  // -> value

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE_FALSE(e.value(lain::flow::PortAddress{gate, graph.node(gate).output(0).id()}).empty());
	REQUIRE(e.value(lain::flow::PortAddress{gate, graph.node(gate).output(0).id()}).get<int>() == 5);

	static_cast<ConstantNode&>(graph.node(enable)).setValue(false);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{gate, graph.node(gate).output(0).id()}).empty()); // suppressed — no value produced
}

TEST_CASE("Merge forwards the first live of its variadic branches (if/else via two gates)", "[flow][nodes]")
{
	Graph graph;
	const NodeId enA = graph.add(constantOf(false)); // branch a gated off
	const NodeId enB = graph.add(constantOf(true));	 // branch b on
	const NodeId va = graph.add(constantOf(3));
	const NodeId vb = graph.add(constantOf(7));
	const NodeId ga = graph.add<GateNode>(portType<int>());
	const NodeId gb = graph.add<GateNode>(portType<int>());
	const NodeId merge = graph.add<MergeNode>(portType<int>());
	// The merge is empty at construction (the dynamic-side contract) — add its branches at runtime.
	auto& mergeNode = static_cast<MergeNode&>(graph.node(merge));
	mergeNode.addDynamicPort<int>("a");
	mergeNode.addDynamicPort<int>("b");
	graph.connect(enA, 0, ga, 0);
	graph.connect(va, 0, ga, 1);
	graph.connect(enB, 0, gb, 0);
	graph.connect(vb, 0, gb, 1);
	graph.connect(ga, 0, merge, 0); // branch a (optional) — empty (gated off)
	graph.connect(gb, 0, merge, 1); // branch b (optional) — 7

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{merge, graph.node(merge).output(0).id()}).get<int>() == 7); // picked the live branch b

	// flip the condition: a on, b off -> merge now forwards a (first live branch)
	static_cast<ConstantNode&>(graph.node(enA)).setValue(true);
	static_cast<ConstantNode&>(graph.node(enB)).setValue(false);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{merge, graph.node(merge).output(0).id()}).get<int>() == 3);
}

TEST_CASE("Select routes among its variadic branches by a connectable selector input", "[flow][nodes]")
{
	Graph graph;
	const NodeId sel = graph.add(constantOf(0)); // the selector, driven by a ConstantNode
	const NodeId va = graph.add(constantOf(10));
	const NodeId vb = graph.add(constantOf(20));
	const NodeId select = graph.add<SelectNode>(portType<int>());
	auto& selectNode = static_cast<SelectNode&>(graph.node(select));
	selectNode.addDynamicPort<int>("a"); // branch 0 (input 1 — the selector is input 0)
	selectNode.addDynamicPort<int>("b"); // branch 1 (input 2)
	graph.connect(sel, 0, select, 0);	 // -> selector input
	graph.connect(va, 0, select, 1);	 // -> branch a
	graph.connect(vb, 0, select, 2);	 // -> branch b

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{select, graph.node(select).output(0).id()}).get<int>() == 10); // selector 0 -> branch a

	static_cast<ConstantNode&>(graph.node(sel)).setValue(1);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{select, graph.node(select).output(0).id()}).get<int>() == 20); // selector 1 -> branch b

	// selector out of range -> no output (nothing to route)
	static_cast<ConstantNode&>(graph.node(sel)).setValue(5);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{select, graph.node(select).output(0).id()}).empty());
}

TEST_CASE("an unwired Gate passes its value through", "[flow][nodes][default]")
{
	// `enable` defaults to true, so a gate dropped on the canvas with nothing attached is
	// TRANSPARENT rather than a dead end. Before input defaults it was a plain required input: an
	// unwired gate was never ready, so it suppressed everything downstream and read as broken until
	// you found a Constant<bool> to feed it.
	Graph graph;
	const NodeId source = graph.add(constantOf(42));
	const NodeId gate = graph.add<GateNode>(portType<int>());
	REQUIRE(graph.connect(source, 0, gate, 1) == Connection::Ok); // -> value; `enable` left unwired

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	REQUIRE(evaluation.ready(gate));
	REQUIRE(lain::flow::test::output(graph, evaluation, gate, 0).get<int>() == 42);

	SECTION("and the default can be flipped to keep it off with nothing attached")
	{
		Node& node = graph.node(gate);
		const Param* fallback = node.defaultOf(node.input(0).id()); // `enable`
		REQUIRE(fallback != nullptr);
		REQUIRE(node.setParam<bool>(fallback->id(), false));

		SerialScheduler{}.run(graph, evaluation);
		REQUIRE(lain::flow::test::output(graph, evaluation, gate, 0).empty()); // suppressed, as an off gate should
	}

	SECTION("a wired enable still wins over the default")
	{
		const NodeId enable = graph.add(constantOf(false));
		REQUIRE(graph.connect(enable, 0, gate, 0) == Connection::Ok);

		SerialScheduler{}.run(graph, evaluation);
		REQUIRE(lain::flow::test::output(graph, evaluation, gate, 0).empty());
	}
}

TEST_CASE("an unwired Select routes branch 0, and a suppressed selector suppresses", "[flow][nodes][default]")
{
	// `selector` was an Optional input with a presence check in compute(); it is now an input with a
	// default, which says the same thing in the declaration instead of in the body. The behaviour
	// that CHANGED is the wired-but-empty case: an Optional selector quietly fell back to branch 0,
	// where a defaulted one leaves the node unready, so a suppressed selector suppresses — the same
	// rule the Gate follows, and the reason a defaulted input stays Required.
	Graph graph;
	const NodeId a = graph.add(constantOf(10));
	const NodeId b = graph.add(constantOf(20));
	const NodeId select = graph.add<SelectNode>(portType<int>());
	auto& node = static_cast<SelectNode&>(graph.node(select));
	const lain::flow::PortId branchA = node.addDynamicPort<int>("a");
	const lain::flow::PortId branchB = node.addDynamicPort<int>("b");
	REQUIRE(graph.connect(lain::flow::PortAddress{a, graph.node(a).output(0).id()},
						  lain::flow::PortAddress{select, branchA}) == Connection::Ok);
	REQUIRE(graph.connect(lain::flow::PortAddress{b, graph.node(b).output(0).id()},
						  lain::flow::PortAddress{select, branchB}) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(lain::flow::test::output(graph, evaluation, select, 0).get<int>() == 10); // unwired -> branch 0

	SECTION("a wired selector picks its branch")
	{
		const NodeId sel = graph.add(constantOf(1));
		REQUIRE(graph.connect(sel, 0, select, 0) == Connection::Ok); // input 0 is `selector`
		SerialScheduler{}.run(graph, evaluation);
		REQUIRE(lain::flow::test::output(graph, evaluation, select, 0).get<int>() == 20);
	}
}
