// Tests for the scheduler — the full push run (serial, and the Taskflow-backed
// parallel one) and the pull evaluate. Pure CPU nodes, so no driver is needed; the
// wide-graph case exercises concurrent task execution for correctness.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"

#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain::flow;

namespace
{
	struct ConstInt : Node
	{
		int value;
		PortId out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(value); }
	};

	struct AddInt : Node
	{
		PortId a, b, sum;
		AddInt()
			: Node("Add")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			sum = addOutput<int>("sum");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(sum).set(evaluation.input(a).get<int>() + evaluation.input(b).get<int>());
		}
	};

	// Constant-like: counts how many times it actually computed.
	struct Counter : Node
	{
		int& calls; // a test probe held by reference, so const compute can still bump it
		PortId out;
		explicit Counter(int& c)
			: Node("Counter")
			, calls(c)
		{
			out = addOutput<int>("v");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(calls);
		}
	};

	// On-request source: asks its OWN evaluation to recompute it, so each pull refires it. That is
	// the point of the request living in the evaluation — one stream refiring says nothing about
	// another stream running the same definition.
	struct Source : Node
	{
		int& calls;
		PortId out;
		explicit Source(int& c)
			: Node("Source")
			, calls(c)
		{
			out = addOutput<int>("v");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(calls);
			evaluation.requestRecompute();
		}
	};

	// A payload whose copies are VISIBLE, for the per-edge-copy guard below.
	struct Tracked
	{
		static int copies;
		Tracked() = default;
		Tracked(const Tracked&) { ++copies; }
		Tracked& operator=(const Tracked&)
		{
			++copies;
			return *this;
		}
		Tracked(Tracked&&) = default;
		Tracked& operator=(Tracked&&) = default;
	};
	int Tracked::copies = 0;

	struct MakeTracked : Node
	{
		PortId out;
		MakeTracked()
			: Node("MakeTracked")
		{
			out = addOutput<Tracked>("v");
		}
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(Tracked{}); }
	};

	// Reads its input by const reference (the ordinary shape for a node that inspects a
	// payload without modifying it) and publishes an unrelated result.
	struct ReadTracked : Node
	{
		PortId in, out;
		ReadTracked()
			: Node("ReadTracked")
		{
			in = addInput<Tracked>("in");
			out = addOutput<int>("seen");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			const Tracked& seen = evaluation.input(in).get<Tracked>();
			(void)seen;
			evaluation.output(out).set(1);
		}
	};
} // namespace

TEST_CASE("push run evaluates the whole graph", "[scheduler]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(2);
	const NodeId c2 = g.add<ConstInt>(3);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, add, 1) == Connection::Ok);

	Evaluation e{g};
	lain::task::Executor executor;
	ParallelScheduler{executor}.run(g, e);

	REQUIRE(e.value(PortAddress{add, g.node(add).output(0).id()}).get<int>() == 5);
	REQUIRE_FALSE(e.needsRecompute(add)); // the evaluation recorded it at the definition's version
	REQUIRE_FALSE(e.needsRecompute(c1));
}

TEST_CASE("serial run evaluates the whole graph in one pass", "[scheduler]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(2);
	const NodeId c2 = g.add<ConstInt>(3);
	const NodeId add = g.add<AddInt>();
	g.connect(c1, 0, add, 0);
	g.connect(c2, 0, add, 1);

	Evaluation e{g};
	SerialScheduler{}.run(g, e);

	REQUIRE(e.value(PortAddress{add, g.node(add).output(0).id()}).get<int>() == 5);
	REQUIRE_FALSE(e.needsRecompute(add));
}

TEST_CASE("push run flows multi-level dependencies", "[scheduler]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(2);
	const NodeId c2 = g.add<ConstInt>(3);
	const NodeId add = g.add<AddInt>();
	const NodeId c3 = g.add<ConstInt>(10);
	const NodeId add2 = g.add<AddInt>();
	g.connect(c1, 0, add, 0);
	g.connect(c2, 0, add, 1);
	g.connect(add, 0, add2, 0); // (2+3) ...
	g.connect(c3, 0, add2, 1);	// ... + 10

	Evaluation e{g};
	lain::task::Executor executor;
	ParallelScheduler{executor}.run(g, e);

	REQUIRE(e.value(PortAddress{add2, g.node(add2).output(0).id()}).get<int>() == 15);
}

TEST_CASE("push run computes independent branches correctly", "[scheduler]")
{
	Graph g;
	const NodeId a = g.add<ConstInt>(1);
	const NodeId b = g.add<ConstInt>(2);
	const NodeId c = g.add<ConstInt>(3);
	const NodeId d = g.add<ConstInt>(4);
	const NodeId ab = g.add<AddInt>();	  // a + b
	const NodeId cd = g.add<AddInt>();	  // c + d  (independent of ab)
	const NodeId total = g.add<AddInt>(); // ab + cd
	g.connect(a, 0, ab, 0);
	g.connect(b, 0, ab, 1);
	g.connect(c, 0, cd, 0);
	g.connect(d, 0, cd, 1);
	g.connect(ab, 0, total, 0);
	g.connect(cd, 0, total, 1);

	Evaluation e{g};
	lain::task::Executor executor;
	ParallelScheduler{executor}.run(g, e);

	REQUIRE(e.value(PortAddress{total, g.node(total).output(0).id()}).get<int>() == 10);
}

TEST_CASE("pull evaluates only the target's upstream", "[scheduler]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(4);
	const NodeId c2 = g.add<ConstInt>(6);
	const NodeId add = g.add<AddInt>();
	const NodeId unrelated = g.add<ConstInt>(99);
	g.connect(c1, 0, add, 0);
	g.connect(c2, 0, add, 1);

	Evaluation e{g};
	SerialScheduler sched;
	sched.evaluate(g, e, add);

	REQUIRE(e.value(PortAddress{add, g.node(add).output(0).id()}).get<int>() == 10);
	REQUIRE_FALSE(e.needsRecompute(add));
	REQUIRE(e.needsRecompute(unrelated)); // never visited
	REQUIRE(e.value(PortAddress{unrelated, g.node(unrelated).output(0).id()}).empty());
}

TEST_CASE("pull recomputes only dirty nodes", "[scheduler]")
{
	int calls = 0;
	Graph g;
	const NodeId n = g.add<Counter>(calls);
	Evaluation e{g};
	SerialScheduler sched;

	sched.evaluate(g, e, n);
	REQUIRE(calls == 1);
	REQUIRE(e.value(PortAddress{n, g.node(n).output(0).id()}).get<int>() == 1);

	sched.evaluate(g, e, n);
	REQUIRE(calls == 1); // clean -> skipped (the constant case)

	e.requestRecompute(n);
	sched.evaluate(g, e, n);
	REQUIRE(calls == 2); // dirty again -> recomputed
}

TEST_CASE("pull refires an on-request source every time", "[scheduler]")
{
	int calls = 0;
	Graph g;
	const NodeId s = g.add<Source>(calls);
	Evaluation e{g};
	SerialScheduler sched;

	sched.evaluate(g, e, s);
	REQUIRE(calls == 1);
	sched.evaluate(g, e, s);
	REQUIRE(calls == 2); // it re-requested itself -> refired
	sched.evaluate(g, e, s);
	REQUIRE(calls == 3);

	// The request is EVALUATION-local: a second evaluation of this definition has its own.
	Evaluation other{g};
	sched.evaluate(g, other, s);
	REQUIRE(calls == 4);
	REQUIRE(e.needsRecompute(s)); // still armed here, untouched by the other run
}

TEST_CASE("one run is one stage, even with a self-rearming source", "[scheduler][staging]")
{
	// The staging loop (ADR-0014) plans, executes, and plans AGAIN only when the stage deferred
	// something — never merely because more work now looks stale. Terminating on "the next plan is
	// empty" instead would be catastrophic exactly here: an on-request source re-arms itself inside
	// compute(), so a freshly built plan is never empty and the invocation would never return.
	//
	// So a source must fire exactly ONCE per run and stay armed for the NEXT one, which is also what
	// keeps a gui's per-frame run from spinning on a camera capture.
	int calls = 0;
	Graph g;
	const NodeId s = g.add<Source>(calls);
	Evaluation e{g};

	SECTION("serial")
	{
		SerialScheduler sched;
		sched.run(g, e);
		REQUIRE(calls == 1);
		REQUIRE(e.needsRecompute(s)); // armed for the next run, not for another stage of this one

		sched.run(g, e);
		REQUIRE(calls == 2);
	}

	SECTION("parallel")
	{
		// Both strategies share the one staging loop and differ only in executePlan, so the
		// termination rule cannot drift between them — this pins that down.
		lain::task::Executor executor;
		ParallelScheduler sched{executor};
		sched.run(g, e);
		REQUIRE(calls == 1);
		REQUIRE(e.needsRecompute(s));

		sched.run(g, e);
		REQUIRE(calls == 2);
	}
}

TEST_CASE("populateInputs shares a payload rather than copying it", "[scheduler][portvalue]")
{
	Tracked::copies = 0;

	Graph g;
	const NodeId src = g.add<MakeTracked>();
	const NodeId a = g.add<ReadTracked>();
	const NodeId b = g.add<ReadTracked>(); // fan-out: two edges off one output

	REQUIRE(g.connect(src, 0, a, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, b, 0) == Connection::Ok);

	Evaluation e{g};
	SerialScheduler sched;
	sched.run(g, e);
	e.requestRecomputeAll();
	sched.run(g, e); // a second full run: a copying slot charges per edge, per run

	REQUIRE(Tracked::copies == 0);
	REQUIRE(e.value(PortAddress{a, g.node(a).output(0).id()}).get<int>() == 1);
	REQUIRE(e.value(PortAddress{b, g.node(b).output(0).id()}).get<int>() == 1);
	// The consumers read the producer's payload itself, not a duplicate of it.
	const PortAddress produced{src, g.node(src).output(0).id()};
	REQUIRE(&e.value(PortAddress{a, g.node(a).input(0).id()}).get<Tracked>() == &e.value(produced).get<Tracked>());
	REQUIRE(&e.value(PortAddress{b, g.node(b).input(0).id()}).get<Tracked>() == &e.value(produced).get<Tracked>());
}
