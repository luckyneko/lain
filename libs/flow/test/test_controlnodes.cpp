// The generic control nodes (WORK.md Tier A #2, slice 2): Constant, Gate, Select, Merge, wired into
// the readiness mechanism (ADR-0007). A Gate suppresses downstream when off; a Merge/Select forwards
// a live branch past a gated-off (empty) one.

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

	SerialScheduler scheduler;
	scheduler.run(graph);
	REQUIRE(graph.node(c).output(0).get<int>() == 42);

	static_cast<ConstantNode<int>&>(graph.node(c)).setValue(7);
	scheduler.run(graph);
	REQUIRE(graph.node(c).output(0).get<int>() == 7);
}

TEST_CASE("Gate passes its value when enabled and suppresses when not", "[flow][nodes]")
{
	Graph graph;
	const NodeId enable = graph.add<ConstantNode<bool>>(true);
	const NodeId value = graph.add<ConstantNode<int>>(5);
	const NodeId gate = graph.add<GateNode<int>>();
	graph.connect(enable, 0, gate, 0); // -> enable
	graph.connect(value, 0, gate, 1);  // -> value

	SerialScheduler scheduler;
	scheduler.run(graph);
	REQUIRE(graph.node(gate).output(0).ready());
	REQUIRE(graph.node(gate).output(0).get<int>() == 5);

	static_cast<ConstantNode<bool>&>(graph.node(enable)).setValue(false);
	scheduler.run(graph);
	REQUIRE_FALSE(graph.node(gate).output(0).ready()); // suppressed — no value produced
}

TEST_CASE("Merge forwards the live branch (if/else via two gates)", "[flow][nodes]")
{
	Graph graph;
	const NodeId enA = graph.add<ConstantNode<bool>>(false); // branch a gated off
	const NodeId enB = graph.add<ConstantNode<bool>>(true);	 // branch b on
	const NodeId va = graph.add<ConstantNode<int>>(3);
	const NodeId vb = graph.add<ConstantNode<int>>(7);
	const NodeId ga = graph.add<GateNode<int>>();
	const NodeId gb = graph.add<GateNode<int>>();
	const NodeId merge = graph.add<MergeNode<int>>();
	graph.connect(enA, 0, ga, 0);
	graph.connect(va, 0, ga, 1);
	graph.connect(enB, 0, gb, 0);
	graph.connect(vb, 0, gb, 1);
	graph.connect(ga, 0, merge, 0); // a (optional) — empty (gated off)
	graph.connect(gb, 0, merge, 1); // b (optional) — 7

	SerialScheduler scheduler;
	scheduler.run(graph);
	REQUIRE(graph.node(merge).output(0).get<int>() == 7); // picked the live branch b

	// flip the condition: a on, b off -> merge now forwards a
	static_cast<ConstantNode<bool>&>(graph.node(enA)).setValue(true);
	static_cast<ConstantNode<bool>&>(graph.node(enB)).setValue(false);
	scheduler.run(graph);
	REQUIRE(graph.node(merge).output(0).get<int>() == 3);
}

TEST_CASE("Select routes by its integer selector", "[flow][nodes]")
{
	Graph graph;
	const NodeId sel = graph.add<ConstantNode<int>>(0);
	const NodeId va = graph.add<ConstantNode<int>>(10);
	const NodeId vb = graph.add<ConstantNode<int>>(20);
	const NodeId select = graph.add<SelectNode<int>>();
	graph.connect(sel, 0, select, 0);
	graph.connect(va, 0, select, 1);
	graph.connect(vb, 0, select, 2);

	SerialScheduler scheduler;
	scheduler.run(graph);
	REQUIRE(graph.node(select).output(0).get<int>() == 10); // selector 0 -> a

	static_cast<ConstantNode<int>&>(graph.node(sel)).setValue(1);
	scheduler.run(graph);
	REQUIRE(graph.node(select).output(0).get<int>() == 20); // selector 1 -> b
}
