// The headline contract of M6 (ADR-0012): one DEFINITION can back N EVALUATIONS, and they do not
// interfere — not with each other's values, not with each other's recompute requests, and not when
// they run at the same time.
//
// These drive the PRODUCTION schedulers rather than a test-only evaluator, because the claim is
// about the real execution path: if `run(const Graph&, Evaluation&)` leaked state through the
// definition, only a real run would show it. The concurrency case is what the `const Graph&` in
// that signature actually promises — including that topoOrder() is no longer a lazy mutable cache
// two runs could rebuild at once.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace lain::flow;

namespace
{
	// Passes its input through, and counts how many times it actually ran. `calls` is a reference to
	// the caller's counter, so a const compute() can still bump it — a test probe, not a pattern.
	struct CountingPass : Node
	{
		std::atomic<int>& calls;
		PortId in, out;
		explicit CountingPass(std::atomic<int>& c)
			: Node("Pass")
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

	// An on-request source: rearms ITS OWN evaluation each run, so each run refires it there.
	struct Rearming : Node
	{
		PortId out;
		Rearming()
			: Node("Rearming")
		{
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			const PortValue& previous = evaluation.output(out);
			evaluation.output(out).set(previous.holds<int>() ? previous.get<int>() + 1 : 1);
			evaluation.requestRecompute();
		}
	};

	// Blocks inside compute() until released — used to hold one run open while another is attempted.
	struct Blocking : Node
	{
		std::atomic<bool>& entered;
		std::atomic<bool>& release;
		PortId out;
		Blocking(std::atomic<bool>& e, std::atomic<bool>& r)
			: Node("Blocking")
			, entered(e)
			, release(r)
		{
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			entered = true;
			while (!release)
				std::this_thread::yield();
			evaluation.output(out).set(1);
		}
	};

	// in -> pass -> out, at the graph's own boundary. Returns {input pin, output pin, pass node}.
	struct Passthrough
	{
		PortId in;
		PortId out;
		NodeId pass;
	};

	Passthrough buildPassthrough(Graph& graph, std::atomic<int>& calls)
	{
		const PortId in = graph.boundaryInputNode().addBoundary<int>("in");
		const PortId out = graph.boundaryOutputNode().addBoundary<int>("out");
		const NodeId pass = graph.add<CountingPass>(calls);
		REQUIRE(graph.connect(PortAddress{graph.boundaryInputNode().id(), in},
							  PortAddress{pass, graph.node(pass).input(0).id()}) == Connection::Ok);
		REQUIRE(graph.connect(PortAddress{pass, graph.node(pass).output(0).id()},
							  PortAddress{graph.boundaryOutputNode().id(), out}) == Connection::Ok);
		return Passthrough{in, out, pass};
	}

	void bindInt(Graph& graph, Evaluation& evaluation, PortId pin, int value)
	{
		PortValue v;
		v.set<int>(value);
		evaluation.bind(PortAddress{graph.boundaryInputNode().id(), pin}, std::move(v));
	}

	int outputOf(Graph& graph, const Evaluation& evaluation, PortId pin)
	{
		return evaluation.value(PortAddress{graph.boundaryOutputNode().id(), pin}).get<int>();
	}
} // namespace

TEST_CASE("two evaluations of one definition keep their values apart", "[flow][evaluation]")
{
	// The target workload in miniature: one recipe, two streams, different data.
	std::atomic<int> calls{0};
	Graph graph;
	const Passthrough p = buildPassthrough(graph, calls);

	Evaluation first{graph};
	Evaluation second{graph};
	bindInt(graph, first, p.in, 11);
	bindInt(graph, second, p.in, 22);

	SerialScheduler scheduler;
	scheduler.run(graph, first);
	scheduler.run(graph, second);

	REQUIRE(outputOf(graph, first, p.out) == 11);
	REQUIRE(outputOf(graph, second, p.out) == 22); // ... and neither overwrote the other
	REQUIRE(calls == 2);						   // each ran the node once, in its own state

	// Re-running one leaves the other exactly as it was.
	bindInt(graph, first, p.in, 33);
	scheduler.run(graph, first);
	REQUIRE(outputOf(graph, first, p.out) == 33);
	REQUIRE(outputOf(graph, second, p.out) == 22);
}

TEST_CASE("two evaluations keep their recompute requests apart", "[flow][evaluation]")
{
	// A request is a demand made of ONE evaluation, so it must not make another re-run. This is why
	// an on-request source (a camera capture) can refire for one stream without disturbing another.
	std::atomic<int> calls{0};
	Graph graph;
	const Passthrough p = buildPassthrough(graph, calls);

	Evaluation first{graph};
	Evaluation second{graph};
	bindInt(graph, first, p.in, 1);
	bindInt(graph, second, p.in, 2);

	SerialScheduler scheduler;
	scheduler.run(graph, first);
	scheduler.run(graph, second);
	REQUIRE(calls == 2);

	first.requestRecompute(p.pass);
	REQUIRE(first.needsRecompute(p.pass));
	REQUIRE_FALSE(second.needsRecompute(p.pass)); // untouched

	scheduler.run(graph, first);
	REQUIRE(calls == 3); // only the one that asked
	scheduler.run(graph, second);
	REQUIRE(calls == 3); // and the other still had nothing to do
}

TEST_CASE("an on-request source rearms only its own evaluation", "[flow][evaluation]")
{
	Graph graph;
	const NodeId source = graph.add<Rearming>();
	const PortAddress out{source, graph.node(source).output(0).id()};

	Evaluation first{graph};
	Evaluation second{graph};
	SerialScheduler scheduler;

	scheduler.run(graph, first);
	scheduler.run(graph, first);
	scheduler.run(graph, first); // it re-requests itself, so each run refires it
	REQUIRE(first.value(out).get<int>() == 3);

	scheduler.run(graph, second);
	REQUIRE(second.value(out).get<int>() == 1); // its own count, started from nothing
	REQUIRE(first.value(out).get<int>() == 3);	// and the other is untouched
}

TEST_CASE("a definition edit invalidates every evaluation, without touching them", "[flow][evaluation]")
{
	// Invalidation is PULLED: the edit bumps a version and each evaluation notices when it next
	// runs. The definition holds no list of its evaluations, so an edit is O(1) in how many exist.
	std::atomic<int> calls{0};
	Graph graph;
	const Passthrough p = buildPassthrough(graph, calls);

	Evaluation first{graph};
	Evaluation second{graph};
	bindInt(graph, first, p.in, 1);
	bindInt(graph, second, p.in, 2);

	SerialScheduler scheduler;
	scheduler.run(graph, first);
	scheduler.run(graph, second);
	REQUIRE_FALSE(first.needsRecompute(p.pass));
	REQUIRE_FALSE(second.needsRecompute(p.pass));

	graph.bumpNodeVersion(p.pass); // a recipe change
	REQUIRE(first.needsRecompute(p.pass));
	REQUIRE(second.needsRecompute(p.pass)); // BOTH notice, though the edit reached neither
}

TEST_CASE("N evaluations run simultaneously over one const definition", "[flow][evaluation]")
{
	// What `const Graph&` in the scheduler signature actually promises. Repeated in the style of
	// the [group] sweep, because a race here would be intermittent — and because it is the
	// regression guard for topoOrder() no longer being a lazy cache two runs could rebuild at once.
	constexpr int kEvaluations = 8;
	constexpr int kRepeats = 100;

	for (int repeat = 0; repeat < kRepeats; ++repeat)
	{
		std::atomic<int> calls{0};
		Graph graph;
		const Passthrough p = buildPassthrough(graph, calls);

		std::vector<Evaluation> evaluations;
		evaluations.reserve(kEvaluations);
		for (int i = 0; i < kEvaluations; ++i)
		{
			evaluations.emplace_back(graph);
			bindInt(graph, evaluations.back(), p.in, i);
		}

		// An edit BEFORE the concurrent runs, so every evaluation is stale and each run really does
		// walk the topological order rather than finding nothing to do.
		graph.bumpNodeVersion(p.pass);

		std::vector<std::thread> threads;
		threads.reserve(kEvaluations);
		for (int i = 0; i < kEvaluations; ++i)
		{
			threads.emplace_back([&graph, &evaluations, i]
								 {
									 SerialScheduler scheduler;
									 scheduler.run(graph, evaluations[static_cast<std::size_t>(i)]); });
		}
		for (std::thread& thread : threads)
			thread.join();

		for (int i = 0; i < kEvaluations; ++i)
			REQUIRE(outputOf(graph, evaluations[static_cast<std::size_t>(i)], p.out) == i);
	}
}

TEST_CASE("the parallel scheduler keeps two evaluations apart too", "[flow][evaluation]")
{
	// Same claim, through the Taskflow lowering — where node tasks of both runs are genuinely
	// interleaved on the pool.
	std::atomic<int> calls{0};
	Graph graph;
	const Passthrough p = buildPassthrough(graph, calls);

	Evaluation first{graph};
	Evaluation second{graph};
	bindInt(graph, first, p.in, 101);
	bindInt(graph, second, p.in, 202);

	lain::task::Executor executor;
	std::thread a([&]
				  { ParallelScheduler{executor}.run(graph, first); });
	std::thread b([&]
				  { ParallelScheduler{executor}.run(graph, second); });
	a.join();
	b.join();

	REQUIRE(outputOf(graph, first, p.out) == 101);
	REQUIRE(outputOf(graph, second, p.out) == 202);
}

TEST_CASE("scheduling one evaluation twice at once is refused, not raced", "[flow][evaluation]")
{
	// A documented precondition alone would leave a release build racing; a blocking mutex would
	// hide the caller error and can deadlock on recursive entry. The lease FAILS, immediately.
	std::atomic<bool> entered{false};
	std::atomic<bool> release{false};
	Graph graph;
	graph.add<Blocking>(entered, release);

	Evaluation evaluation{graph};
	SerialScheduler scheduler;

	std::thread holder([&]
					   { scheduler.run(graph, evaluation); });
	while (!entered)
		std::this_thread::yield(); // the first run is now inside compute(), holding the lease

	REQUIRE_THROWS_AS(scheduler.run(graph, evaluation), std::logic_error);

	release = true;
	holder.join();

	// Unwinding released the lease, so the evaluation is usable again.
	REQUIRE_NOTHROW(scheduler.run(graph, evaluation));
}

TEST_CASE("the lease is released when a node throws", "[flow][evaluation]")
{
	// RAII, not a flag someone has to reset: a compute() that throws must not leave the evaluation
	// permanently unusable.
	struct Throwing : Node
	{
		Throwing()
			: Node("Throwing")
		{
			addOutput<int>("out");
		}
		void compute(NodeEvaluation&) const override { throw std::runtime_error("boom"); }
	};

	Graph graph;
	graph.add<Throwing>();
	Evaluation evaluation{graph};
	SerialScheduler scheduler;

	REQUIRE_THROWS_AS(scheduler.run(graph, evaluation), std::runtime_error);
	REQUIRE_THROWS_AS(scheduler.run(graph, evaluation), std::runtime_error); // ... and not logic_error
}

TEST_CASE("an evaluation is refused against a different definition", "[flow][evaluation]")
{
	// The guard rail, not the mechanism: a host owns a definition and its evaluation as one
	// replaceable unit, which is what actually prevents the mispairing. This catches a call that
	// slipped through — though not a Graph rebuilt at a recycled address, which is why the
	// ownership rule is the real protection.
	Graph first;
	Graph second;
	Evaluation evaluation{first};

	REQUIRE_THROWS_AS(SerialScheduler{}.run(second, evaluation), std::logic_error);
	REQUIRE_NOTHROW(SerialScheduler{}.run(first, evaluation));
}

TEST_CASE("prepare keeps surviving ports' values and drops the rest", "[flow][evaluation]")
{
	// Reconciliation runs on the coordinator, before any task — so a worker only ever touches slots
	// that already exist. A node that gained or lost a pin keeps what it still has.
	std::atomic<int> calls{0};
	Graph graph;
	const Passthrough p = buildPassthrough(graph, calls);

	Evaluation evaluation{graph};
	bindInt(graph, evaluation, p.in, 5);
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(outputOf(graph, evaluation, p.out) == 5);

	// Add a node: prepare must make room for it without disturbing what is already there.
	const NodeId added = graph.add<test::ConstInt>(9);
	evaluation.prepare(graph);
	REQUIRE(evaluation.contains(added));
	REQUIRE(outputOf(graph, evaluation, p.out) == 5); // the existing values survived

	// Remove it: its state goes with it.
	REQUIRE(graph.removeNode(added));
	evaluation.prepare(graph);
	REQUIRE_FALSE(evaluation.contains(added));
	REQUIRE(outputOf(graph, evaluation, p.out) == 5);
}
