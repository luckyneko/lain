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

#include <catch2/catch_test_macros.hpp>

using lain::flow::ConstantNode;
using lain::flow::GateNode;
using lain::flow::Graph;
using lain::flow::MergeNode;
using lain::flow::NodeId;
using lain::flow::SelectNode;
using lain::flow::SerialScheduler;

TEST_CASE("Constant emits its value and re-emits on change", "[flow][nodes]")
{
	Graph graph;
	const NodeId c = graph.add<ConstantNode<int>>(42);

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{c, graph.node(c).output(0).id()}).get<int>() == 42);

	static_cast<ConstantNode<int>&>(graph.node(c)).setValue(7);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{c, graph.node(c).output(0).id()}).get<int>() == 7);
}

TEST_CASE("Gate passes its value when enabled and suppresses when not", "[flow][nodes]")
{
	Graph graph;
	const NodeId enable = graph.add<ConstantNode<bool>>(true);
	const NodeId value = graph.add<ConstantNode<int>>(5);
	const NodeId gate = graph.add<GateNode<int>>();
	graph.connect(enable, 0, gate, 0); // -> enable
	graph.connect(value, 0, gate, 1);  // -> value

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE_FALSE(e.value(lain::flow::PortAddress{gate, graph.node(gate).output(0).id()}).empty());
	REQUIRE(e.value(lain::flow::PortAddress{gate, graph.node(gate).output(0).id()}).get<int>() == 5);

	static_cast<ConstantNode<bool>&>(graph.node(enable)).setValue(false);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{gate, graph.node(gate).output(0).id()}).empty()); // suppressed — no value produced
}

TEST_CASE("Merge forwards the first live of its variadic branches (if/else via two gates)", "[flow][nodes]")
{
	Graph graph;
	const NodeId enA = graph.add<ConstantNode<bool>>(false); // branch a gated off
	const NodeId enB = graph.add<ConstantNode<bool>>(true);	 // branch b on
	const NodeId va = graph.add<ConstantNode<int>>(3);
	const NodeId vb = graph.add<ConstantNode<int>>(7);
	const NodeId ga = graph.add<GateNode<int>>();
	const NodeId gb = graph.add<GateNode<int>>();
	const NodeId merge = graph.add<MergeNode<int>>();
	// The merge is empty at construction (the dynamic-side contract) — add its branches at runtime.
	auto& mergeNode = static_cast<MergeNode<int>&>(graph.node(merge));
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
	static_cast<ConstantNode<bool>&>(graph.node(enA)).setValue(true);
	static_cast<ConstantNode<bool>&>(graph.node(enB)).setValue(false);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{merge, graph.node(merge).output(0).id()}).get<int>() == 3);
}

TEST_CASE("Select routes among its variadic branches by a connectable selector input", "[flow][nodes]")
{
	Graph graph;
	const NodeId sel = graph.add<ConstantNode<int>>(0); // the selector, driven by a ConstantNode
	const NodeId va = graph.add<ConstantNode<int>>(10);
	const NodeId vb = graph.add<ConstantNode<int>>(20);
	const NodeId select = graph.add<SelectNode<int>>();
	auto& selectNode = static_cast<SelectNode<int>&>(graph.node(select));
	selectNode.addDynamicPort<int>("a"); // branch 0 (input 1 — the selector is input 0)
	selectNode.addDynamicPort<int>("b"); // branch 1 (input 2)
	graph.connect(sel, 0, select, 0);	 // -> selector input
	graph.connect(va, 0, select, 1);	 // -> branch a
	graph.connect(vb, 0, select, 2);	 // -> branch b

	lain::flow::Evaluation e{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{select, graph.node(select).output(0).id()}).get<int>() == 10); // selector 0 -> branch a

	static_cast<ConstantNode<int>&>(graph.node(sel)).setValue(1);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{select, graph.node(select).output(0).id()}).get<int>() == 20); // selector 1 -> branch b

	// selector out of range -> no output (nothing to route)
	static_cast<ConstantNode<int>&>(graph.node(sel)).setValue(5);
	scheduler.run(graph, e);
	REQUIRE(e.value(lain::flow::PortAddress{select, graph.node(select).output(0).id()}).empty());
}
