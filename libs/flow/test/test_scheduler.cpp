// Tests for the scheduler — the full push run (serial, and the Taskflow-backed
// parallel one) and the pull evaluate. Pure CPU nodes, so no driver is needed; the
// wide-graph case exercises concurrent task execution for correctness.

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
		PortIndex out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		void compute() override { output(out).set(value); }
	};

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

	// Constant-like: counts how many times it actually computed.
	struct Counter : Node
	{
		int& calls;
		PortIndex out;
		explicit Counter(int& c)
			: Node("Counter")
			, calls(c)
		{
			out = addOutput<int>("v");
		}
		void compute() override
		{
			++calls;
			output(out).set(calls);
		}
	};

	// On-request source: re-marks itself dirty so each pull refires it.
	struct Source : Node
	{
		int& calls;
		PortIndex out;
		explicit Source(int& c)
			: Node("Source")
			, calls(c)
		{
			out = addOutput<int>("v");
		}
		void compute() override
		{
			++calls;
			output(out).set(calls);
			markDirty();
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
		PortIndex out;
		MakeTracked()
			: Node("MakeTracked")
		{
			out = addOutput<Tracked>("v");
		}
		void compute() override { output(out).set(Tracked{}); }
	};

	// Reads its input by const reference (the ordinary shape for a node that inspects a
	// payload without modifying it) and publishes an unrelated result.
	struct ReadTracked : Node
	{
		PortIndex in, out;
		ReadTracked()
			: Node("ReadTracked")
		{
			in = addInput<Tracked>("in");
			out = addOutput<int>("seen");
		}
		void compute() override
		{
			const Tracked& seen = input(in).get<Tracked>();
			(void)seen;
			output(out).set(1);
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

	lain::task::Executor executor;
	ParallelScheduler{executor}.run(g);

	REQUIRE(g.node(add).output(0).get<int>() == 5);
	REQUIRE_FALSE(g.node(add).dirty()); // scheduler cleared it
	REQUIRE_FALSE(g.node(c1).dirty());
}

TEST_CASE("serial run evaluates the whole graph in one pass", "[scheduler]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(2);
	const NodeId c2 = g.add<ConstInt>(3);
	const NodeId add = g.add<AddInt>();
	g.connect(c1, 0, add, 0);
	g.connect(c2, 0, add, 1);

	SerialScheduler{}.run(g);

	REQUIRE(g.node(add).output(0).get<int>() == 5);
	REQUIRE_FALSE(g.node(add).dirty());
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

	lain::task::Executor executor;
	ParallelScheduler{executor}.run(g);

	REQUIRE(g.node(add2).output(0).get<int>() == 15);
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

	lain::task::Executor executor;
	ParallelScheduler{executor}.run(g);

	REQUIRE(g.node(total).output(0).get<int>() == 10);
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

	SerialScheduler sched;
	sched.evaluate(g, add);

	REQUIRE(g.node(add).output(0).get<int>() == 10);
	REQUIRE_FALSE(g.node(add).dirty());
	REQUIRE(g.node(unrelated).dirty()); // never visited
	REQUIRE_FALSE(g.node(unrelated).output(0).ready());
}

TEST_CASE("pull recomputes only dirty nodes", "[scheduler]")
{
	int calls = 0;
	Graph g;
	const NodeId n = g.add<Counter>(calls);
	SerialScheduler sched;

	sched.evaluate(g, n);
	REQUIRE(calls == 1);
	REQUIRE(g.node(n).output(0).get<int>() == 1);

	sched.evaluate(g, n);
	REQUIRE(calls == 1); // clean -> skipped (the constant case)

	g.node(n).markDirty();
	sched.evaluate(g, n);
	REQUIRE(calls == 2); // dirty again -> recomputed
}

TEST_CASE("pull refires an on-request source every time", "[scheduler]")
{
	int calls = 0;
	Graph g;
	const NodeId s = g.add<Source>(calls);
	SerialScheduler sched;

	sched.evaluate(g, s);
	REQUIRE(calls == 1);
	sched.evaluate(g, s);
	REQUIRE(calls == 2); // stayed dirty -> refired
	sched.evaluate(g, s);
	REQUIRE(calls == 3);
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

	SerialScheduler sched;
	sched.run(g);
	g.markAllDirty();
	sched.run(g); // a second full run: a copying slot charges per edge, per run

	REQUIRE(Tracked::copies == 0);
	REQUIRE(g.node(a).output(0).get<int>() == 1);
	REQUIRE(g.node(b).output(0).get<int>() == 1);
	// The consumers read the producer's payload itself, not a duplicate of it.
	REQUIRE(&g.node(a).input(0).get<Tracked>() == &g.node(src).output(0).get<Tracked>());
	REQUIRE(&g.node(b).input(0).get<Tracked>() == &g.node(src).output(0).get<Tracked>());
}
