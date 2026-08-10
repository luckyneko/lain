// Tests for running a NESTED graph — the execution plan (ADR-0009). A group node is never run as
// a node: the scheduler expands the whole nesting tree into one flat plan (entry step, the inner
// graph's own steps, exit step), so a nested graph runs as a single DAG with nothing nested at
// runtime. Driver-free: the payload is int, so nesting is proven without any GPU/image type.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain::flow;

namespace
{
	// Counts how often it recomputed, forwarding its input — for observing WHAT re-ran.
	struct CountingPass : Node
	{
		int& calls;
		PortId in, out;
		explicit CountingPass(int& c)
			: Node("CountingPass")
			, calls(c)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// A group whose inner graph is: GroupInput("in") -> Add(+ ConstInt(k)) -> GroupOutput("out"),
	// with "in" / "out" exposed as the group's own ports. Returns its id in `parent`.
	NodeId addAdderGroup(Graph& parent, int k)
	{
		const NodeId id = parent.add<InlineGroupNode>();
		auto& group = static_cast<InlineGroupNode&>(parent.node(id));
		Graph& inner = group.inner();

		const NodeId innerIn = inner.boundaryInputNode().id();
		const NodeId innerOut = inner.boundaryOutputNode().id();
		const PortId inPin = inner.boundaryInputNode().addBoundary<int>("in");
		const PortId outPin = inner.boundaryOutputNode().addBoundary<int>("out");

		const NodeId konst = inner.add<test::ConstInt>(k);
		const NodeId add = inner.add<test::AddInt>();
		REQUIRE(inner.connect({innerIn, inPin}, {add, inner.node(add).input(0).id()}) == Connection::Ok);
		REQUIRE(inner.connect(konst, 0, add, 1) == Connection::Ok);
		REQUIRE(inner.connect({add, inner.node(add).output(0).id()}, {innerOut, outPin}) == Connection::Ok);

		group.exposeInput<int>("in", inPin);
		group.exposeOutput<int>("out", outPin);
		return id;
	}

	// Wire `group` between the parent's own boundary: x -> group -> y. Returns the two pins.
	std::pair<PortId, PortId> wireThroughBoundary(Graph& g, NodeId group)
	{
		const PortId x = g.boundaryInputNode().addBoundary<int>("x");
		const PortId y = g.boundaryOutputNode().addBoundary<int>("y");
		REQUIRE(g.connect({g.boundaryInputNode().id(), x}, {group, g.node(group).input(0).id()}) == Connection::Ok);
		REQUIRE(g.connect({group, g.node(group).output(0).id()}, {g.boundaryOutputNode().id(), y}) == Connection::Ok);
		return {x, y};
	}

	// Binding is an EVALUATION operation now: the boundary node holds no value, which is what lets
	// two evaluations of one graph be driven differently.
	void bind(Graph& g, Evaluation& e, PortId pin, int value)
	{
		PortValue v;
		v.set<int>(value);
		e.bind(PortAddress{g.boundaryInputNode().id(), pin}, std::move(v));
	}
} // namespace

TEST_CASE("a value crosses into a group, through its inner graph, and back out", "[flow][group][scheduler]")
{
	Graph g;
	const NodeId group = addAdderGroup(g, 100);
	const auto [x, y] = wireThroughBoundary(g, group);
	Evaluation e{g};
	bind(g, e, x, 5);

	SerialScheduler().run(g, e);

	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).holds<int>());
	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 105);

	SECTION("re-binding flows a new value through the group")
	{
		bind(g, e, x, 7);
		SerialScheduler().run(g, e);
		REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 107);
	}

	SECTION("the group's own outer port carries the result too")
	{
		// The parent reads a group exactly like any other node — its outputs are real ports.
		REQUIRE(e.value(PortAddress{group, g.node(group).output(0).id()}).get<int>() == 105);
	}
}

TEST_CASE("nesting goes arbitrarily deep", "[flow][group][scheduler]")
{
	// outer group { inner group { +10 } , then +100 } — two levels, both expanded into one plan.
	Graph g;
	const NodeId outer = g.add<InlineGroupNode>();
	auto& outerGroup = static_cast<InlineGroupNode&>(g.node(outer));
	Graph& mid = outerGroup.inner();

	const NodeId midIn = mid.boundaryInputNode().id();
	const NodeId midOut = mid.boundaryOutputNode().id();
	const PortId midInPin = mid.boundaryInputNode().addBoundary<int>("in");
	const PortId midOutPin = mid.boundaryOutputNode().addBoundary<int>("out");

	const NodeId deep = addAdderGroup(mid, 10); // the nested group, one level further down
	REQUIRE(mid.connect({midIn, midInPin}, {deep, mid.node(deep).input(0).id()}) == Connection::Ok);

	const NodeId konst = mid.add<test::ConstInt>(100);
	const NodeId add = mid.add<test::AddInt>();
	REQUIRE(mid.connect({deep, mid.node(deep).output(0).id()}, {add, mid.node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(mid.connect(konst, 0, add, 1) == Connection::Ok);
	REQUIRE(mid.connect({add, mid.node(add).output(0).id()}, {midOut, midOutPin}) == Connection::Ok);

	outerGroup.exposeInput<int>("in", midInPin);
	outerGroup.exposeOutput<int>("out", midOutPin);

	const auto [x, y] = wireThroughBoundary(g, outer);
	Evaluation e{g};
	bind(g, e, x, 1);

	SerialScheduler().run(g, e);
	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 111); // 1 + 10 + 100
}

TEST_CASE("serial and parallel agree over a nested graph", "[flow][group][scheduler]")
{
	// The equivalence that matters: the flat plan must mean the same thing however it is executed.
	// Two sibling groups make the parallel case actually interleave inner work.
	auto build = [](Graph& g)
	{
		const NodeId a = addAdderGroup(g, 10);
		const NodeId b = addAdderGroup(g, 20);
		const PortId x = g.boundaryInputNode().addBoundary<int>("x");
		const PortId ya = g.boundaryOutputNode().addBoundary<int>("ya");
		const PortId yb = g.boundaryOutputNode().addBoundary<int>("yb");
		REQUIRE(g.connect({g.boundaryInputNode().id(), x}, {a, g.node(a).input(0).id()}) == Connection::Ok);
		REQUIRE(g.connect({g.boundaryInputNode().id(), x}, {b, g.node(b).input(0).id()}) == Connection::Ok);
		REQUIRE(g.connect({a, g.node(a).output(0).id()}, {g.boundaryOutputNode().id(), ya}) == Connection::Ok);
		REQUIRE(g.connect({b, g.node(b).output(0).id()}, {g.boundaryOutputNode().id(), yb}) == Connection::Ok);
		return std::pair<PortId, PortId>{ya, yb};
	};
	const auto feed = [](Graph& g, Evaluation& e, PortId pin)
	{
		PortValue v;
		v.set<int>(1);
		e.bind(PortAddress{g.boundaryInputNode().id(), pin}, std::move(v));
	};

	Graph serial;
	const auto [sa, sb] = build(serial);
	Evaluation serialEval{serial};
	feed(serial, serialEval, serial.boundaryInputs().front().port.port);
	SerialScheduler().run(serial, serialEval);

	Graph parallel;
	const auto [pa, pb] = build(parallel);
	Evaluation parallelEval{parallel};
	feed(parallel, parallelEval, parallel.boundaryInputs().front().port.port);
	lain::task::Executor executor;
	ParallelScheduler{executor}.run(parallel, parallelEval);

	const auto out = [](Graph& g, Evaluation& e, PortId pin)
	{ return e.value(PortAddress{g.boundaryOutputNode().id(), pin}).get<int>(); };
	REQUIRE(out(serial, serialEval, sa) == 11);
	REQUIRE(out(serial, serialEval, sb) == 21);
	REQUIRE(out(parallel, parallelEval, pa) == out(serial, serialEval, sa));
	REQUIRE(out(parallel, parallelEval, pb) == out(serial, serialEval, sb));
}

TEST_CASE("suppression crosses a group boundary with no extra machinery", "[flow][group][scheduler]")
{
	// ADR-0009: an unready group publishes EMPTY values; the inner nodes suppress themselves through
	// the ordinary readiness gate (ADR-0007) and the emptiness reaches the inner GroupOutput, so the
	// exit step copies out nothing.
	Graph g;
	const NodeId group = addAdderGroup(g, 100);
	const PortId y = g.boundaryOutputNode().addBoundary<int>("y");
	REQUIRE(g.connect({group, g.node(group).output(0).id()}, {g.boundaryOutputNode().id(), y}) == Connection::Ok);
	// The group's input is left unconnected, so it is never ready.

	Evaluation e{g};
	SerialScheduler().run(g, e);

	REQUIRE(e.value(PortAddress{group, g.node(group).output(0).id()}).empty()); // nothing produced
	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).empty());		// and nothing delivered downstream

	SECTION("the suppression is visible inside the group too")
	{
		Graph& inner = static_cast<InlineGroupNode&>(g.node(group)).inner();
		bool sawSuppressedAdder = false;
		for (const NodeId id : inner.nodeIds())
		{
			if (inner.node(id).name() == "Add")
			{
				REQUIRE_FALSE(e.child(group).ready(id)); // its required input never got a value
				sawSuppressedAdder = true;
			}
		}
		REQUIRE(sawSuppressedAdder);
	}
}

TEST_CASE("an edit inside a group re-runs only that group's dirty closure", "[flow][group][scheduler]")
{
	// The publish gate: a group in the run ONLY because something inside it is dirty must not
	// republish its inputs, or the inner boundary would go dirty and drag the whole inner graph
	// back through recompute — losing inner incrementality.
	int fedByBoundary = 0; // a chain downstream of the inner GroupInput
	int independent = 0;   // a chain that does not touch the boundary

	Graph g;
	const NodeId group = g.add<InlineGroupNode>();
	auto& groupNode = static_cast<InlineGroupNode&>(g.node(group));
	Graph& inner = groupNode.inner();

	const NodeId innerIn = inner.boundaryInputNode().id();
	const NodeId innerOut = inner.boundaryOutputNode().id();
	const PortId inPin = inner.boundaryInputNode().addBoundary<int>("in");
	const PortId outPin = inner.boundaryOutputNode().addBoundary<int>("out");

	const NodeId fed = inner.add<CountingPass>(fedByBoundary);
	REQUIRE(inner.connect({innerIn, inPin}, {fed, inner.node(fed).input(0).id()}) == Connection::Ok);
	REQUIRE(inner.connect({fed, inner.node(fed).output(0).id()}, {innerOut, outPin}) == Connection::Ok);

	const NodeId source = inner.add<test::ConstInt>(1);
	const NodeId other = inner.add<CountingPass>(independent);
	REQUIRE(inner.connect(source, 0, other, 0) == Connection::Ok);

	groupNode.exposeInput<int>("in", inPin);
	groupNode.exposeOutput<int>("out", outPin);
	const auto [x, y] = wireThroughBoundary(g, group);
	Evaluation e{g};
	bind(g, e, x, 5);

	SerialScheduler sched;
	sched.run(g, e);
	REQUIRE(fedByBoundary == 1);
	REQUIRE(independent == 1);
	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 5);

	SECTION("a clean graph re-runs nothing")
	{
		sched.run(g, e);
		REQUIRE(fedByBoundary == 1);
		REQUIRE(independent == 1);
	}

	SECTION("dirtying an inner node re-runs it WITHOUT republishing the group's inputs")
	{
		// Demand the inner node in the group's CHILD evaluation — that is where inner staleness
		// lives now, and the outer plan reaches it by asking the child rather than by walking flags
		// on the inner definition.
		e.child(group).requestRecompute(other);

		sched.run(g, e);
		REQUIRE(independent == 2);	 // the dirty inner chain recomputed
		REQUIRE(fedByBoundary == 1); // the boundary-fed chain did NOT — inputs never republished
	}

	SECTION("changing the group's input DOES republish, re-running the boundary-fed chain")
	{
		bind(g, e, x, 9);
		sched.run(g, e);
		REQUIRE(fedByBoundary == 2);
		REQUIRE(independent == 1); // and only that chain
		REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 9);
	}
}

TEST_CASE("the pull path crosses a group too", "[flow][group][scheduler]")
{
	Graph g;
	const NodeId group = addAdderGroup(g, 100);
	const auto [x, y] = wireThroughBoundary(g, group);
	Evaluation e{g};
	bind(g, e, x, 5);

	// evaluate() plans the dirty upstream cone of its target — through the group, same as run().
	SerialScheduler().evaluate(g, e, g.boundaryOutputNode().id());
	REQUIRE(e.value(PortAddress{g.boundaryOutputNode().id(), y}).get<int>() == 105);
}
