// Incremental re-eval (WORK.md Tier A #3): run() recomputes only the dirty closure — a dirty node
// plus everything downstream of one — and skips clean nodes (keeping their cached value). Graph's
// structural mutations mark the affected node dirty; markAllDirty() forces a full pass.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h" // test::output — positional value lookup

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>

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
	std::unique_ptr<lain::flow::Node> clone() const override { return std::make_unique<CountingSource>(*this); }
	void compute(lain::flow::NodeEvaluation& evaluation) const override
	{
		++*m_computes;
		evaluation.output(m_out).set<int>(value);
	}
	// A TEST PROBE, not a pattern: compute() is const because a node must not write to its
	// definition (several evaluations may run it at once), so the count lives OUTSIDE the node and
	// the node holds a pointer to it — which is also what lets a clone share it, since clone() copies
	// the node (ADR-0025). Atomic so the counter stays meaningful if a case ever runs this node in
	// parallel.
	int computes() const { return *m_computes; }
	int value = 1;

private:
	std::shared_ptr<std::atomic<int>> m_computes = std::make_shared<std::atomic<int>>(0);
	lain::flow::PortId m_out;
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
	std::unique_ptr<lain::flow::Node> clone() const override { return std::make_unique<CountingRelay>(*this); }
	void compute(lain::flow::NodeEvaluation& evaluation) const override
	{
		++*m_computes;
		const lain::flow::PortValue& in = evaluation.input(m_in);
		evaluation.output(m_out).set<int>(in.holds<int>() ? in.get<int>() : 0);
	}
	int computes() const { return *m_computes; } // a test probe — see CountingSource

private:
	std::shared_ptr<std::atomic<int>> m_computes = std::make_shared<std::atomic<int>>(0);
	lain::flow::PortId m_in;
	lain::flow::PortId m_out;
};

// A passthrough whose compute() throws while it is armed — a node failing on the input it was
// handed. Counted, so a test can see whether a later run tried it again.
class ThrowingRelay : public lain::flow::Node
{
public:
	ThrowingRelay()
		: lain::flow::Node("ThrowingRelay")
	{
		m_in = addInput<int>("in");
		m_out = addOutput<int>("out");
	}
	std::unique_ptr<lain::flow::Node> clone() const override { return std::make_unique<ThrowingRelay>(*this); }
	void compute(lain::flow::NodeEvaluation& evaluation) const override
	{
		++*m_computes;
		if (*m_armed)
			throw std::runtime_error("ThrowingRelay: armed");
		evaluation.output(m_out).set<int>(evaluation.input(m_in).get<int>());
	}
	int computes() const { return *m_computes; } // test probes — see CountingSource
	void arm(bool armed) { *m_armed = armed; }

private:
	std::shared_ptr<std::atomic<int>> m_computes = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<bool>> m_armed = std::make_shared<std::atomic<bool>>(false);
	lain::flow::PortId m_in;
	lain::flow::PortId m_out;
};

// Source -> ThrowingRelay, where the relay is in the second run ONLY because its source changed: its
// own version still equals what it was computed at. The run that throws must leave it owed a
// compute, so the next run retries it on the new input rather than serving the old one.
static void checkThrowStaysStale(lain::flow::Scheduler& scheduler)
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId t = graph.add<ThrowingRelay>();
	REQUIRE(graph.connect(s, 0, t, 0) == Connection::Ok);
	auto& src = static_cast<CountingSource&>(graph.node(s));
	auto& relay = static_cast<ThrowingRelay&>(graph.node(t));

	lain::flow::Evaluation evaluation{graph};
	scheduler.run(graph, evaluation);
	REQUIRE(relay.computes() == 1);
	REQUIRE(lain::flow::test::output(graph, evaluation, t, 0).get<int>() == 1);

	src.value = 2;
	evaluation.requestRecompute(s); // an evaluation-local request: the relay's version is untouched
	relay.arm(true);
	REQUIRE_THROWS_AS(scheduler.run(graph, evaluation), std::runtime_error);
	REQUIRE(relay.computes() == 2);
	REQUIRE(evaluation.needsRecompute(t)); // the node that threw is still owed a compute

	relay.arm(false);
	scheduler.run(graph, evaluation);
	REQUIRE(relay.computes() == 3);
	REQUIRE(lain::flow::test::output(graph, evaluation, t, 0).get<int>() == 2);
}

TEST_CASE("a node that throws stays stale when only its input changed", "[flow][incremental]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkThrowStaysStale(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkThrowStaysStale(scheduler);
	}
}

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

	lain::flow::Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation); // a fresh evaluation knows nothing -> everything runs once
	REQUIRE(src.computes() == 1);
	REQUIRE(mid.computes() == 1);
	REQUIRE(sink.computes() == 1);

	scheduler.run(graph, evaluation); // nothing stale -> nothing recomputes
	REQUIRE(src.computes() == 1);
	REQUIRE(mid.computes() == 1);
	REQUIRE(sink.computes() == 1);

	evaluation.requestRecompute(m);	  // demand a recompute of the middle node
	scheduler.run(graph, evaluation); // mid + its downstream (sink) rerun; the source does not
	REQUIRE(src.computes() == 1);
	REQUIRE(mid.computes() == 2);
	REQUIRE(sink.computes() == 2);

	// Force a full refresh. Addressed to THIS evaluation — a definition cannot know which of several
	// evaluations a caller wants refreshed, which is why Graph::markAllDirty could not survive.
	evaluation.requestRecomputeAll();
	scheduler.run(graph, evaluation);
	REQUIRE(src.computes() == 2);
	REQUIRE(mid.computes() == 3);
	REQUIRE(sink.computes() == 3);
}

TEST_CASE("connect marks the downstream node dirty (not the source)", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId k = graph.add<CountingRelay>();

	lain::flow::Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation); // source runs; the relay's required input is unconnected -> not ready
	auto& src = static_cast<CountingSource&>(graph.node(s));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));
	REQUIRE(src.computes() == 1);
	REQUIRE(sink.computes() == 0); // empty required input -> suppressed (readiness model)

	REQUIRE(graph.connect(s, 0, k, 0) == Connection::Ok); // bumps k's version; it now has an input source
	scheduler.run(graph, evaluation);
	REQUIRE(sink.computes() == 1); // recomputed — now ready
	REQUIRE(src.computes() == 1);  // the (clean) source was skipped
}

TEST_CASE("disconnect and removeNode re-evaluate (and suppress) the affected downstream", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId m = graph.add<CountingRelay>();
	const NodeId k = graph.add<CountingRelay>();
	graph.connect(s, 0, m, 0);
	graph.connect(m, 0, k, 0);

	lain::flow::Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation); // all connected -> all compute -> all produce
	REQUIRE_FALSE(lain::flow::test::output(graph, evaluation, m, 0).empty());
	REQUIRE_FALSE(lain::flow::test::output(graph, evaluation, k, 0).empty());

	graph.disconnect(m, 0); // m loses its source -> its version bumps; m + downstream k re-evaluate
	scheduler.run(graph, evaluation);
	REQUIRE(lain::flow::test::output(graph, evaluation, m, 0).empty()); // suppressed: required input empty
	REQUIRE(lain::flow::test::output(graph, evaluation, k, 0).empty()); // suppression propagated downstream

	// reconnect so k has an input again, then removing m must re-suppress k
	graph.connect(s, 0, m, 0);
	scheduler.run(graph, evaluation);
	REQUIRE_FALSE(lain::flow::test::output(graph, evaluation, k, 0).empty());

	graph.removeNode(m); // k loses its input source -> its version bumps -> not ready
	scheduler.run(graph, evaluation);
	REQUIRE(lain::flow::test::output(graph, evaluation, k, 0).empty());
}

TEST_CASE("the parallel scheduler is incremental too", "[flow][incremental]")
{
	Graph graph;
	const NodeId s = graph.add<CountingSource>();
	const NodeId k = graph.add<CountingRelay>();
	graph.connect(s, 0, k, 0);

	lain::flow::Evaluation evaluation{graph};
	lain::testing::ThreadPool pool;
	ParallelScheduler scheduler;
	scheduler.run(graph, evaluation); // a fresh evaluation -> everything runs
	auto& src = static_cast<CountingSource&>(graph.node(s));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));
	REQUIRE(src.computes() == 1);
	REQUIRE(sink.computes() == 1);

	scheduler.run(graph, evaluation); // nothing stale -> no tasks run
	REQUIRE(src.computes() == 1);
	REQUIRE(sink.computes() == 1);

	evaluation.requestRecompute(k);
	scheduler.run(graph, evaluation); // only k
	REQUIRE(src.computes() == 1);
	REQUIRE(sink.computes() == 2);
}
