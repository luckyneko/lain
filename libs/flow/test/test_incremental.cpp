// Incremental re-eval (WORK.md Tier A #3): run() recomputes only the dirty closure — a dirty node
// plus everything downstream of one — and skips clean nodes (keeping their cached value). Graph's
// structural mutations mark the affected node dirty; markAllDirty() forces a full pass.

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/scheduler.h"

#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

using lain::flow::Connection;
using lain::flow::Graph;
using lain::flow::NodeId;
using lain::flow::ParallelScheduler;
using lain::flow::SerialScheduler;

// A source that counts its computes and emits a settable value.
class CountingSource : public lain::flow::Node
{
public:
	CountingSource()
		: lain::flow::Node("Source")
	{
		m_out = addOutput<int>("out");
	}
	void compute() override
	{
		++computes;
		output(m_out).set<int>(value);
	}
	int computes = 0;
	int value = 1;

private:
	lain::flow::PortIndex m_out = 0;
};

// A passthrough that counts its computes (so a test can see whether it re-ran).
class CountingRelay : public lain::flow::Node
{
public:
	CountingRelay()
		: lain::flow::Node("Relay")
	{
		m_in = addInput<int>("in");
		m_out = addOutput<int>("out");
	}
	void compute() override
	{
		++computes;
		output(m_out).set<int>(input(m_in).holds<int>() ? input(m_in).get<int>() : 0);
	}
	int computes = 0;

private:
	lain::flow::PortIndex m_in = 0;
	lain::flow::PortIndex m_out = 0;
};

TEST_CASE("run recomputes only the dirty closure, skipping clean nodes", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId m = graph.add<CountingRelay>();
	const NodeId k = graph.add<CountingRelay>();
	REQUIRE(graph.connect(s, 0, m, 0) == Connection::Ok);
	REQUIRE(graph.connect(m, 0, k, 0) == Connection::Ok);

	auto& src = static_cast<CountingSource&>(graph.node(s));
	auto& mid = static_cast<CountingRelay&>(graph.node(m));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));

	SerialScheduler scheduler;
	scheduler.run(graph); // fresh graph: everything dirty -> everything runs once
	REQUIRE(src.computes == 1);
	REQUIRE(mid.computes == 1);
	REQUIRE(sink.computes == 1);

	scheduler.run(graph); // nothing dirty -> nothing recomputes
	REQUIRE(src.computes == 1);
	REQUIRE(mid.computes == 1);
	REQUIRE(sink.computes == 1);

	graph.node(m).markDirty(); // an edit to the middle node
	scheduler.run(graph);	   // mid + its downstream (sink) rerun; the source does not
	REQUIRE(src.computes == 1);
	REQUIRE(mid.computes == 2);
	REQUIRE(sink.computes == 2);

	graph.markAllDirty(); // force a full refresh
	scheduler.run(graph);
	REQUIRE(src.computes == 2);
	REQUIRE(mid.computes == 3);
	REQUIRE(sink.computes == 3);
}

TEST_CASE("connect marks the downstream node dirty (not the source)", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId k = graph.add<CountingRelay>();

	SerialScheduler scheduler;
	scheduler.run(graph); // both run once, then clean
	auto& src = static_cast<CountingSource&>(graph.node(s));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));
	REQUIRE(src.computes == 1);
	REQUIRE(sink.computes == 1);

	REQUIRE(graph.connect(s, 0, k, 0) == Connection::Ok); // marks k dirty
	scheduler.run(graph);
	REQUIRE(sink.computes == 2); // recomputed with the new input
	REQUIRE(src.computes == 1);	 // the (clean) source was skipped
}

TEST_CASE("disconnect and removeNode mark the affected downstream dirty", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId m = graph.add<CountingRelay>();
	const NodeId k = graph.add<CountingRelay>();
	graph.connect(s, 0, m, 0);
	graph.connect(m, 0, k, 0);

	SerialScheduler scheduler;
	scheduler.run(graph);
	auto& mid = static_cast<CountingRelay&>(graph.node(m));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));

	graph.disconnect(m, 0); // m loses its source -> m dirty; sink is downstream of m
	scheduler.run(graph);
	REQUIRE(mid.computes == 2);
	REQUIRE(sink.computes == 2);

	graph.removeNode(m); // k loses its input source -> k dirty
	scheduler.run(graph);
	REQUIRE(sink.computes == 3);
}

TEST_CASE("the parallel scheduler is incremental too", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId k = graph.add<CountingRelay>();
	graph.connect(s, 0, k, 0);

	lain::task::Executor executor;
	ParallelScheduler scheduler{executor};
	scheduler.run(graph); // everything dirty -> runs
	auto& src = static_cast<CountingSource&>(graph.node(s));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));
	REQUIRE(src.computes == 1);
	REQUIRE(sink.computes == 1);

	scheduler.run(graph); // clean -> no tasks run
	REQUIRE(src.computes == 1);
	REQUIRE(sink.computes == 1);

	graph.node(k).markDirty();
	scheduler.run(graph); // only k
	REQUIRE(src.computes == 1);
	REQUIRE(sink.computes == 2);
}
