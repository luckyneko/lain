// Tests for the scheduler — Graph::run (push) and Graph::evaluate (pull). Pure
// CPU nodes, so no driver is needed; the wide-graph case exercises concurrent
// task execution for correctness.

#include <lain/flow/graph.h>
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
	g.run(executor);

	REQUIRE(g.node(add).output(0).get<int>() == 5);
	REQUIRE_FALSE(g.node(add).dirty()); // scheduler cleared it
	REQUIRE_FALSE(g.node(c1).dirty());
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

	lain::task::Executor executor; // the injected-executor overload
	g.run(executor);

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
	g.run(executor);

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

	g.evaluate(add);

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

	g.evaluate(n);
	REQUIRE(calls == 1);
	REQUIRE(g.node(n).output(0).get<int>() == 1);

	g.evaluate(n);
	REQUIRE(calls == 1); // clean -> skipped (the constant case)

	g.node(n).markDirty();
	g.evaluate(n);
	REQUIRE(calls == 2); // dirty again -> recomputed
}

TEST_CASE("pull refires an on-request source every time", "[scheduler]")
{
	int calls = 0;
	Graph g;
	const NodeId s = g.add<Source>(calls);

	g.evaluate(s);
	REQUIRE(calls == 1);
	g.evaluate(s);
	REQUIRE(calls == 2); // stayed dirty -> refired
	g.evaluate(s);
	REQUIRE(calls == 3);
}
