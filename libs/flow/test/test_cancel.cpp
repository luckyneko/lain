// Cancelling a run (M14 slice 3, ADR-0025): a host supersedes an in-flight run whenever an edit
// lands, so a cancel has to leave the evaluation exactly where the NEXT run computes the right answer
// — not where the stopped one happened to get to.
//
// What each case pins:
//   * everything a cancelled (or thrown-out-of) run did not reach stays STALE — the planned closure
//     is persisted as recompute requests before a stage runs, since staleness otherwise assumes a
//     run finishes its closure;
//   * a compute that finishes after the cancel is KEPT, and one that asks and gives up is NOT;
//   * the crossings between levels still run, so a group's entry publishes even when the cancel
//     landed just before it;
//   * a map stopped part-way is found by staleness in EVERY element, not only the first;
//   * progress is live, and summed across stages.
// A loop stopping between iterations lives with the loop suite (test_loop.cpp, [cancel]).
//
// And what a THROW leaves (M14 slice 6, [failure]): a record against the node that threw, in the
// evaluation it threw in — so a host can show a failure where it happened (a map element, a
// linked-group instance) — cleared by that node's next compute that gets through.
//
// Every case compares its re-run against a run nobody cancelled, over the same edited graph: a
// weaker check ("it produced a value") passes just as well when a stale value survives.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/runcontrol.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"
#include "testscene.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lain::flow;
using namespace lain::flow::test;

namespace lain::flow::test::cancel
{
	// Where a test arms a node to cancel: the run's control while armed, nullptr otherwise. Held by
	// REFERENCE, so a clone of the node arms from the same place and a test disarms it for the re-run
	// without editing the definition — a disarm that bumped a version would recompute the node and
	// hide exactly what these cases measure.
	using Trigger = RunControl*;

	// An int passthrough that cancels the run computing it when armed, then FINISHES NORMALLY — it
	// never asks whether it was cancelled. What a slow load does when the edit lands while it runs.
	struct Canceller : Node
	{
		std::atomic<int>& calls;
		Trigger& trigger;
		PortId in, out;
		Canceller(std::atomic<int>& c, Trigger& t)
			: Node("Canceller")
			, calls(c)
			, trigger(t)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Canceller>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			if (trigger != nullptr)
				trigger->cancel();
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// Cancels when armed, then ASKS — and gives up without writing: a long compute polling
	// NodeEvaluation::cancelled() and returning early.
	struct Bailer : Node
	{
		std::atomic<int>& calls;
		Trigger& trigger;
		PortId in, out;
		Bailer(std::atomic<int>& c, Trigger& t)
			: Node("Bailer")
			, calls(c)
			, trigger(t)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Bailer>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			if (trigger != nullptr)
				trigger->cancel();
			if (evaluation.cancelled())
				return;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// int + a param offset, cancelling when armed and its input equals `at` — then finishing
	// normally. The body a map runs once per element, so the cancel lands part-way through the map.
	struct OffsetCancelAt : Node
	{
		std::atomic<int>& calls;
		Trigger& trigger;
		int at;
		PortId offset, in, out;
		OffsetCancelAt(std::atomic<int>& c, Trigger& t, int cancelAt)
			: Node("OffsetCancelAt")
			, calls(c)
			, trigger(t)
			, at(cancelAt)
		{
			offset = addParam<int>("offset", 10);
			in = addInput<int>("x");
			out = addOutput<int>("y");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<OffsetCancelAt>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			const int x = evaluation.input(in).get<int>();
			if (trigger != nullptr && x == at)
				trigger->cancel();
			evaluation.output(out).set(x + param(offset).get<int>());
		}
	};

	// An int passthrough that throws while armed — a compute failing part-way down a run.
	struct ThrowWhenArmed : Node
	{
		bool& armed;
		PortId in, out;
		explicit ThrowWhenArmed(bool& a)
			: Node("ThrowWhenArmed")
			, armed(a)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<ThrowWhenArmed>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			if (armed)
				throw std::runtime_error("ThrowWhenArmed: armed");
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// An int passthrough that records how far the run it is computing in has got — the progress a
	// host's status bar would read from another thread, read here from inside the run.
	struct ProgressProbe : Node
	{
		Trigger& control;
		std::size_t& seenFinished;
		std::size_t& seenPlanned;
		PortId in, out;
		ProgressProbe(Trigger& c, std::size_t& finished, std::size_t& planned)
			: Node("ProgressProbe")
			, control(c)
			, seenFinished(finished)
			, seenPlanned(planned)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<ProgressProbe>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			if (control != nullptr)
			{
				seenFinished = control->finished();
				seenPlanned = control->planned();
			}
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// source -> middle -> sink, with the middle node supplied: the chain a cancel stops half-way
	// down. Not copyable, since its nodes hold references into it.
	template <typename Middle>
	struct Chain
	{
		Graph graph;
		std::atomic<int> sourceCalls{0};
		std::atomic<int> middleCalls{0};
		std::atomic<int> sinkCalls{0};
		Trigger trigger = nullptr;
		NodeId source, middle, sink;

		Chain()
		{
			source = graph.add<Source>(sourceCalls, 1);
			middle = graph.add<Middle>(middleCalls, trigger);
			sink = graph.add<Relay>(sinkCalls);
			REQUIRE(graph.connect(source, 0, middle, 0) == Connection::Ok);
			REQUIRE(graph.connect(middle, 0, sink, 0) == Connection::Ok);
		}
		Chain(const Chain&) = delete;

		// A recipe edit at the top of the chain: a version bump on the source ALONE, so the two
		// nodes below it are in the next run only because of it — the closure, not their own state.
		void setSource(int value)
		{
			auto& node = static_cast<Source&>(graph.node(source));
			REQUIRE(node.setParam(node.value, value));
		}
	};
} // namespace lain::flow::test::cancel

using namespace lain::flow::test::cancel;

// What a run nobody cancelled makes of `graph` at `node` — the answer a cancelled run followed by a
// re-run must reach. A fresh evaluation, so nothing the cancelled one retained can leak in.
static int cleanAnswer(const Graph& graph, NodeId node)
{
	Evaluation clean{graph};
	SerialScheduler{}.run(graph, clean);
	return intOut(graph, clean, node);
}

// Run `chain` once cleanly, edit its source, then run again under a control its middle node cancels.
static void cancelHalfway(Chain<Canceller>& chain, Scheduler& scheduler, Evaluation& evaluation)
{
	scheduler.run(chain.graph, evaluation);
	REQUIRE(intOut(chain.graph, evaluation, chain.sink) == 1);

	chain.setSource(2);
	RunControl control;
	chain.trigger = &control;
	scheduler.run(chain.graph, evaluation, control);
	chain.trigger = nullptr;
	REQUIRE(control.cancelled());
}

static void checkUnreachedStaysStale(Scheduler& scheduler)
{
	Chain<Canceller> chain;
	Evaluation evaluation{chain.graph};
	cancelHalfway(chain, scheduler, evaluation);

	// The sink never started — and it was in this run ONLY because its source changed, so nothing
	// about its own recipe says it is owed a compute. The persisted closure is what does.
	REQUIRE(chain.sinkCalls == 1);
	REQUIRE(intOut(chain.graph, evaluation, chain.sink) == 1);
	REQUIRE(evaluation.needsRecompute(chain.sink));

	scheduler.run(chain.graph, evaluation);
	REQUIRE(chain.sinkCalls == 2);
	REQUIRE(intOut(chain.graph, evaluation, chain.sink) == cleanAnswer(chain.graph, chain.sink));
	REQUIRE(intOut(chain.graph, evaluation, chain.sink) == 2);
}

TEST_CASE("a cancelled run leaves every node it did not reach stale", "[flow][cancel]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkUnreachedStaysStale(scheduler);
	}
	SECTION("parallel")
	{
		// The sink depends on the canceller, so it starts only after the cancel landed — the same
		// cut in both strategies.
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkUnreachedStaysStale(scheduler);
	}
}

static void checkFinishedStepKept(Scheduler& scheduler)
{
	Chain<Canceller> chain;
	Evaluation evaluation{chain.graph};
	cancelHalfway(chain, scheduler, evaluation);
	REQUIRE(chain.middleCalls == 2);
	REQUIRE_FALSE(evaluation.needsRecompute(chain.middle));

	// The canceller finished normally, on the recipe the edit produced — so its result is right, and
	// the next run takes it up rather than repeating it. A slow load finished before a tint was
	// tweaked is not loaded again.
	scheduler.run(chain.graph, evaluation);
	REQUIRE(chain.middleCalls == 2);
	REQUIRE(chain.sinkCalls == 2);
	REQUIRE(intOut(chain.graph, evaluation, chain.sink) == 2);
}

TEST_CASE("a step that finishes after the cancel is kept", "[flow][cancel]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkFinishedStepKept(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkFinishedStepKept(scheduler);
	}
}

static void checkBailerStaysStale(Scheduler& scheduler)
{
	Chain<Bailer> chain;
	Evaluation evaluation{chain.graph};
	scheduler.run(chain.graph, evaluation);
	REQUIRE(intOut(chain.graph, evaluation, chain.middle) == 1);

	chain.setSource(2);
	RunControl control;
	chain.trigger = &control;
	scheduler.run(chain.graph, evaluation, control);
	chain.trigger = nullptr;

	// It asked, heard yes and gave up — so what it holds is last run's, and it is owed a compute.
	REQUIRE(chain.middleCalls == 2);
	REQUIRE(intOut(chain.graph, evaluation, chain.middle) == 1);
	REQUIRE(evaluation.needsRecompute(chain.middle));
	REQUIRE(control.finished() == 1); // the source only: a node that gave up did not get through

	scheduler.run(chain.graph, evaluation);
	REQUIRE(chain.middleCalls == 3);
	REQUIRE(intOut(chain.graph, evaluation, chain.middle) == 2);
	REQUIRE(intOut(chain.graph, evaluation, chain.sink) == cleanAnswer(chain.graph, chain.sink));
}

TEST_CASE("a node that gives up on a cancel stays stale", "[flow][cancel]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkBailerStaysStale(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkBailerStaysStale(scheduler);
	}
}

static void checkEntryStillPublishes(Scheduler& scheduler)
{
	// source -> canceller -> [group: relay] -> sink. The cancel lands in the canceller, just before the
	// group's entry. The entry is a CROSSING, so it still runs and publishes the canceller's new
	// value into the interior. Were it skipped, the group would be selected next run only for its
	// stale interior, would not republish (nothing upstream of it changes again), and its interior
	// would compute the OLD input forever.
	Graph graph;
	std::atomic<int> sourceCalls{0}, cancellerCalls{0}, innerCalls{0}, sinkCalls{0};
	Trigger trigger = nullptr;
	const NodeId source = graph.add<Source>(sourceCalls, 1);
	const NodeId canceller = graph.add<Canceller>(cancellerCalls, trigger);
	const NodeId group = graph.add<InlineGroupNode>();
	buildInterior(graph, group, static_cast<InlineGroupNode&>(graph.node(group)).inner(), std::make_unique<Relay>(innerCalls));
	const NodeId sink = graph.add<Relay>(sinkCalls);
	REQUIRE(graph.connect(source, 0, canceller, 0) == Connection::Ok);
	REQUIRE(graph.connect(canceller, 0, group, 0) == Connection::Ok);
	REQUIRE(graph.connect(group, 0, sink, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	scheduler.run(graph, evaluation);
	REQUIRE(intOut(graph, evaluation, sink) == 1);

	auto& src = static_cast<Source&>(graph.node(source));
	REQUIRE(src.setParam(src.value, 2));
	RunControl control;
	trigger = &control;
	scheduler.run(graph, evaluation, control);
	trigger = nullptr;
	REQUIRE(innerCalls == 1); // the interior never computed...
	REQUIRE(sinkCalls == 1);  // ...nor anything past the group

	scheduler.run(graph, evaluation);
	REQUIRE(innerCalls == 2);
	REQUIRE(cancellerCalls == 2); // kept, as ever
	REQUIRE(intOut(graph, evaluation, sink) == cleanAnswer(graph, sink));
	REQUIRE(intOut(graph, evaluation, sink) == 2);
}

TEST_CASE("a cancel just before a group still lets its entry publish", "[flow][cancel][group]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkEntryStillPublishes(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkEntryStillPublishes(scheduler);
	}
}

TEST_CASE("a map stopped part-way is found stale in every element", "[flow][cancel][map]")
{
	// list {1,2,3} -> map[x + offset, cancelling at x == 2]. The edit is INSIDE the interior (its
	// offset param), so the map is in the run only because its interior is stale — it is not
	// re-prepared, and each element runs its own closure. The cancel lands in element 1; its gather
	// still runs (a crossing) over a map part-finished. What says the map is still owed is element 1's
	// and element 2's staleness — which asking element 0 alone would miss, since it finished.
	registerSceneTypes();
	Graph graph;
	std::atomic<int> listCalls{0}, elementCalls{0};
	Trigger trigger = nullptr;
	const NodeId list = graph.add<MakeInts>(listCalls, Ints{1, 2, 3});
	const NodeId map = graph.add<MapNode>();
	Graph& inner = static_cast<MapNode&>(graph.node(map)).inner();
	const NodeId body = buildInterior(graph, map, inner, std::make_unique<OffsetCancelAt>(elementCalls, trigger, 2));
	REQUIRE(graph.connect(list, 0, map, 0) == Connection::Ok);

	const auto mapped = [&](const Evaluation& evaluation)
	{ return output(graph, evaluation, map, 0).get<Ints>(); };

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(mapped(evaluation) == Ints{11, 12, 13});
	REQUIRE(elementCalls == 3);

	auto& bodyNode = static_cast<OffsetCancelAt&>(inner.node(body));
	REQUIRE(bodyNode.setParam(bodyNode.offset, 100));

	SECTION("serial")
	{
		// Elements run in order, so the cut is exact: element 0 finished, element 1's body finished
		// and cancelled, element 2 never started — and the gather still ran over what was there.
		RunControl control;
		trigger = &control;
		SerialScheduler{}.run(graph, evaluation, control);
		trigger = nullptr;
		REQUIRE(elementCalls == 5);
		REQUIRE(mapped(evaluation) == Ints{101, 12, 13});

		// Only element 2's body is still owed: the two that finished are kept.
		SerialScheduler{}.run(graph, evaluation);
		REQUIRE(elementCalls == 6);
	}
	SECTION("parallel")
	{
		// The elements are independent branches, so where the cut falls is the pool's business.
		// Whatever it was, the re-run must still reach the clean answer.
		lain::testing::ThreadPool pool;
		RunControl control;
		trigger = &control;
		ParallelScheduler{}.run(graph, evaluation, control);
		trigger = nullptr;
		ParallelScheduler{}.run(graph, evaluation);
	}

	REQUIRE(mapped(evaluation) == Ints{101, 102, 103});
	Evaluation clean{graph};
	SerialScheduler{}.run(graph, clean);
	REQUIRE(mapped(evaluation) == output(graph, clean, map, 0).get<Ints>());
}

static void checkThrowLeavesUnreachedStale(Scheduler& scheduler, bool serial)
{
	// source -> thrower -> after, and source -> beside. A throw ends a run early: a serial walk stops
	// where it is, abandoning BESIDE, which has nothing to do with the thrower and is in the run only
	// because the source changed. The closure keeps it owed — which is what lets a host mark the
	// thrower Failed and still trust everything else. (The pool keeps running independent branches
	// and skips only the thrower's successors, which the thrower's own re-request already selects —
	// so there it is the serial walk this guards, and the parallel section pins the same answer.)
	Graph graph;
	std::atomic<int> sourceCalls{0}, afterCalls{0}, besideCalls{0};
	bool armed = false;
	const NodeId source = graph.add<Source>(sourceCalls, 1);
	const NodeId thrower = graph.add<ThrowWhenArmed>(armed);
	const NodeId after = graph.add<Relay>(afterCalls);
	const NodeId beside = graph.add<Relay>(besideCalls);
	REQUIRE(graph.connect(source, 0, beside, 0) == Connection::Ok);
	REQUIRE(graph.connect(source, 0, thrower, 0) == Connection::Ok);
	REQUIRE(graph.connect(thrower, 0, after, 0) == Connection::Ok);

	// The serial cut only means something if beside comes AFTER the thrower in the walk — asserted
	// rather than assumed, so a change to the topo order fails here instead of quietly testing less.
	const std::vector<NodeId>& order = graph.topoOrder();
	REQUIRE(std::find(order.begin(), order.end(), thrower) < std::find(order.begin(), order.end(), beside));

	Evaluation evaluation{graph};
	scheduler.run(graph, evaluation);
	REQUIRE(intOut(graph, evaluation, beside) == 1);

	auto& src = static_cast<Source&>(graph.node(source));
	REQUIRE(src.setParam(src.value, 2));
	armed = true;
	REQUIRE_THROWS_AS(scheduler.run(graph, evaluation), std::runtime_error);
	REQUIRE(afterCalls == 1); // never reached, whichever strategy ran it
	if (serial)
	{
		REQUIRE(besideCalls == 1);
		REQUIRE(evaluation.needsRecompute(beside));
	}

	armed = false;
	scheduler.run(graph, evaluation);
	REQUIRE(intOut(graph, evaluation, after) == 2);
	REQUIRE(intOut(graph, evaluation, beside) == 2);
	REQUIRE(intOut(graph, evaluation, beside) == cleanAnswer(graph, beside));
}

TEST_CASE("a throw leaves every node the run did not reach stale", "[flow][cancel]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkThrowLeavesUnreachedStale(scheduler, true);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkThrowLeavesUnreachedStale(scheduler, false);
	}
}

TEST_CASE("progress is live while the run is in flight", "[flow][cancel]")
{
	// source -> relay -> probe: by the time the probe computes, the two nodes before it have got
	// through, and all five were planned — the three, plus the graph's own boundary pair, which every
	// graph is born with and which a user sees on the canvas like any node. Read from INSIDE the run,
	// which is what a host's frame loop does from outside it — a count kept only at the end would read
	// 0 here.
	Graph graph;
	std::atomic<int> sourceCalls{0}, relayCalls{0};
	Trigger control = nullptr;
	std::size_t seenFinished = 0;
	std::size_t seenPlanned = 0;
	const NodeId source = graph.add<Source>(sourceCalls, 1);
	const NodeId relay = graph.add<Relay>(relayCalls);
	const NodeId probe = graph.add<ProgressProbe>(control, seenFinished, seenPlanned);
	REQUIRE(graph.connect(source, 0, relay, 0) == Connection::Ok);
	REQUIRE(graph.connect(relay, 0, probe, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	RunControl run;
	control = &run;
	SerialScheduler{}.run(graph, evaluation, run);

	REQUIRE(seenFinished == 2);
	REQUIRE(seenPlanned == 5);
	REQUIRE(run.planned() == 5);
	REQUIRE(run.finished() == 5);
}

TEST_CASE("progress is summed across stages and counts only node computes", "[flow][cancel][map]")
{
	// The scene's first stage runs the root's boundary pair, source, the group's three inner nodes,
	// sink and the list, and defers the map; the second runs the map's three elements, three inner
	// nodes each. The group's entry and exit and the map's gather are crossings, and are not counted:
	// the count is of what a user sees on the canvas.
	Graph graph;
	Calls calls;
	buildScene(graph, calls);
	Evaluation evaluation{graph};
	RunControl control;
	SerialScheduler{}.run(graph, evaluation, control);

	REQUIRE(control.planned() == 8 + 9);
	REQUIRE(control.finished() == control.planned());
}

TEST_CASE("a run cancelled before it starts computes nothing", "[flow][cancel]")
{
	Graph graph;
	Calls calls;
	const Scene scene = buildScene(graph, calls);
	Evaluation evaluation{graph};

	RunControl control;
	control.cancel();
	SerialScheduler{}.run(graph, evaluation, control);
	REQUIRE(calls.source == 0);
	REQUIRE(calls.list == 0);
	REQUIRE(control.planned() == 0);

	// Nothing was touched, so a run nobody cancels still does all of it.
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(calls.element == 3);
	REQUIRE(intOut(graph, evaluation, scene.sink) == 1);
}

TEST_CASE("the stage observer is called between stages, never after the last", "[flow][cancel][map]")
{
	// What a gui host builds on it (ADR-0025): a copy of the evaluation, taken where nothing is
	// running, so a map's or a loop's progress shows before the whole run returns. The scene runs in
	// two stages — the root and the list, then the map's elements — so the observer fires exactly
	// once, and what it sees is the FIRST stage: the collection the map is about to map over is
	// there, and no element has run.
	SECTION("a run in two stages calls it once, between them")
	{
		Calls calls;
		Graph document;
		const Scene s = buildScene(document, calls);
		const auto clone = std::make_shared<const Graph>(document.clone());
		Evaluation working{document};

		int observed = 0;
		int elementsAtObservation = -1;
		std::vector<int> listAtObservation;
		RunControl control;
		control.setStageObserver([&]()
								 {
			++observed;
			elementsAtObservation = calls.element;
			// The use it exists for, and the proof the evaluation is readable here: publishing
			// checks that the pair is the one the run prepared, and copies every slot.
			const PublishedEvaluation published{clone, working};
			listAtObservation = output(*clone, published.evaluation(), s.list, 0).get<Ints>(); });
		SerialScheduler{}.run(*clone, working, control);

		REQUIRE(observed == 1);
		REQUIRE(elementsAtObservation == 0);
		REQUIRE(listAtObservation == Ints{1, 2, 3});
		REQUIRE(calls.element == 3); // and the run carried on past it
	}

	SECTION("a run in one stage never calls it")
	{
		// run() returning is that boundary, and a host already knows when that happens — so a graph
		// with no map and no loop is observed not at all, rather than once at the end.
		Graph graph;
		std::atomic<int> sourceCalls{0}, relayCalls{0};
		const NodeId source = graph.add<Source>(sourceCalls, 1);
		const NodeId relay = graph.add<Relay>(relayCalls);
		REQUIRE(graph.connect(source, 0, relay, 0) == Connection::Ok);

		Evaluation evaluation{graph};
		int observed = 0;
		RunControl control;
		control.setStageObserver([&]()
								 { ++observed; });
		SerialScheduler{}.run(graph, evaluation, control);
		REQUIRE(observed == 0);
		REQUIRE(relayCalls == 1);
	}

	SECTION("a run cancelled in its first stage never calls it")
	{
		// The cancel lands in the stage BEFORE the map, so there is no next stage to be between.
		Calls calls;
		Graph graph;
		const Scene s = buildScene(graph, calls);
		std::atomic<int> cancellerCalls{0};
		Trigger trigger = nullptr;
		const NodeId canceller = graph.add<Canceller>(cancellerCalls, trigger);
		REQUIRE(graph.connect(s.source, 0, canceller, 0) == Connection::Ok);

		Evaluation evaluation{graph};
		int observed = 0;
		RunControl control;
		trigger = &control;
		control.setStageObserver([&]()
								 { ++observed; });
		SerialScheduler{}.run(graph, evaluation, control);
		REQUIRE(cancellerCalls == 1);
		REQUIRE(observed == 0);
		REQUIRE(calls.element == 0);
	}
}

//=========================================================================
// What a throw leaves behind (M14 slice 6): a record of it, at the node that threw
//=========================================================================

namespace lain::flow::test::failure
{
	// An int passthrough that throws something that is NOT a std::exception while armed.
	struct ThrowIntWhenArmed : Node
	{
		bool& armed;
		PortId in, out;
		explicit ThrowIntWhenArmed(bool& a)
			: Node("ThrowIntWhenArmed")
			, armed(a)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<ThrowIntWhenArmed>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			if (armed)
				throw 42;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// int -> int, throwing on one value: a map body that fails in exactly one element.
	struct ThrowAt : Node
	{
		int at;
		PortId in, out;
		explicit ThrowAt(int value)
			: Node("ThrowAt")
			, at(value)
		{
			in = addInput<int>("x");
			out = addOutput<int>("y");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<ThrowAt>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			const int x = evaluation.input(in).get<int>();
			if (x == at)
				throw std::runtime_error("ThrowAt: element holding " + std::to_string(x));
			evaluation.output(out).set(x);
		}
	};

	// source -> thrower -> after: the chain a throw is recorded in, armed through a reference.
	template <typename Thrower>
	struct Failing
	{
		Graph graph;
		std::atomic<int> sourceCalls{0}, afterCalls{0};
		bool armed = false;
		NodeId source, thrower, after;

		Failing()
		{
			source = graph.add<Source>(sourceCalls, 1);
			thrower = graph.add<Thrower>(armed);
			after = graph.add<Relay>(afterCalls);
			REQUIRE(graph.connect(source, 0, thrower, 0) == Connection::Ok);
			REQUIRE(graph.connect(thrower, 0, after, 0) == Connection::Ok);
		}
		Failing(const Failing&) = delete;

		void setSource(int value)
		{
			auto& node = static_cast<Source&>(graph.node(source));
			REQUIRE(node.setParam(node.value, value));
		}
	};
} // namespace lain::flow::test::failure

using namespace lain::flow::test::failure;

static void checkThrowRecorded(Scheduler& scheduler)
{
	Failing<ThrowWhenArmed> chain;
	Evaluation evaluation{chain.graph};
	scheduler.run(chain.graph, evaluation);
	REQUIRE(evaluation.failure(chain.thrower) == nullptr);
	REQUIRE_FALSE(evaluation.hasFailure());

	chain.armed = true;
	chain.setSource(2);
	RunControl control;
	REQUIRE_THROWS_AS(scheduler.run(chain.graph, evaluation, control), std::runtime_error);

	// Recorded where it happened, with what it said — and counted, which is how a host tells a
	// throw some node owned from one none did.
	const std::string* failure = evaluation.failure(chain.thrower);
	REQUIRE(failure != nullptr);
	REQUIRE(*failure == "ThrowWhenArmed: armed");
	REQUIRE(evaluation.failure(chain.source) == nullptr);
	REQUIRE(evaluation.failure(chain.after) == nullptr);
	REQUIRE(evaluation.hasFailure());
	REQUIRE(control.failed() == 1);
	REQUIRE(evaluation.needsRecompute(chain.thrower)); // and it is still owed

	// Cleared by the next compute that gets through.
	chain.armed = false;
	RunControl clean;
	scheduler.run(chain.graph, evaluation, clean);
	REQUIRE(evaluation.failure(chain.thrower) == nullptr);
	REQUIRE_FALSE(evaluation.hasFailure());
	REQUIRE(clean.failed() == 0);
	REQUIRE(intOut(chain.graph, evaluation, chain.after) == 2);
}

TEST_CASE("a throw is recorded against the node that threw, until it computes again", "[flow][cancel][failure]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkThrowRecorded(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkThrowRecorded(scheduler);
	}
}

TEST_CASE("an exception that is not a std::exception is still described", "[flow][cancel][failure]")
{
	Failing<ThrowIntWhenArmed> chain;
	Evaluation evaluation{chain.graph};
	chain.armed = true;
	REQUIRE_THROWS_AS(SerialScheduler{}.run(chain.graph, evaluation), int); // the type that left is the one thrown
	const std::string* failure = evaluation.failure(chain.thrower);
	REQUIRE(failure != nullptr);
	REQUIRE_FALSE(failure->empty());
}

TEST_CASE("a failure is cleared when the node is suppressed instead", "[flow][cancel][failure]")
{
	// Suppression is a compute that got through (ADR-0007): the node is clean and empty together, and
	// what it threw last time no longer describes it.
	Failing<ThrowWhenArmed> chain;
	Evaluation evaluation{chain.graph};
	chain.armed = true;
	REQUIRE_THROWS_AS(SerialScheduler{}.run(chain.graph, evaluation), std::runtime_error);
	REQUIRE(evaluation.failure(chain.thrower) != nullptr);

	REQUIRE(chain.graph.disconnect(PortAddress{chain.thrower, chain.graph.node(chain.thrower).input(0).id()}));
	SerialScheduler{}.run(chain.graph, evaluation); // still armed, but its required input is empty now
	REQUIRE_FALSE(evaluation.ready(chain.thrower));
	REQUIRE(evaluation.failure(chain.thrower) == nullptr);
}

TEST_CASE("a failure survives a run that never reached the node", "[flow][cancel][failure]")
{
	// source -> canceller -> thrower. The thrower fails; then a run is cancelled in the canceller, so
	// it never reaches the thrower — whose last compute still threw, and whose record says so.
	Graph graph;
	std::atomic<int> sourceCalls{0}, cancellerCalls{0};
	Trigger trigger = nullptr;
	bool armed = true;
	const NodeId source = graph.add<Source>(sourceCalls, 1);
	const NodeId canceller = graph.add<Canceller>(cancellerCalls, trigger);
	const NodeId thrower = graph.add<ThrowWhenArmed>(armed);
	REQUIRE(graph.connect(source, 0, canceller, 0) == Connection::Ok);
	REQUIRE(graph.connect(canceller, 0, thrower, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	REQUIRE_THROWS_AS(SerialScheduler{}.run(graph, evaluation), std::runtime_error);
	REQUIRE(evaluation.failure(thrower) != nullptr);

	armed = false;
	auto& src = static_cast<Source&>(graph.node(source));
	REQUIRE(src.setParam(src.value, 2));
	RunControl control;
	trigger = &control;
	SerialScheduler{}.run(graph, evaluation, control);
	trigger = nullptr;
	REQUIRE(control.cancelled());
	REQUIRE(evaluation.failure(thrower) != nullptr);

	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(evaluation.failure(thrower) == nullptr);
}

TEST_CASE("a failure in a map is recorded in the element that threw", "[flow][cancel][failure][map]")
{
	// The NodeId of the body names it in every element alike; which ELEMENT failed is known only
	// because the record sits in that element's own evaluation.
	registerSceneTypes();
	Graph graph;
	std::atomic<int> listCalls{0};
	const NodeId list = graph.add<MakeInts>(listCalls, Ints{1, 2, 3});
	const NodeId map = graph.add<MapNode>();
	Graph& inner = static_cast<MapNode&>(graph.node(map)).inner();
	const NodeId body = buildInterior(graph, map, inner, std::make_unique<ThrowAt>(2));
	REQUIRE(graph.connect(list, 0, map, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	RunControl control;
	REQUIRE_THROWS_AS(SerialScheduler{}.run(graph, evaluation, control), std::runtime_error);
	REQUIRE(control.failed() == 1);
	REQUIRE(evaluation.hasFailure());
	REQUIRE(evaluation.failure(map) == nullptr); // the map itself did not throw
	REQUIRE(evaluation.childCount(map) == 3);
	REQUIRE(evaluation.child(map, 0).failure(body) == nullptr);
	REQUIRE(evaluation.child(map, 1).failure(body) != nullptr);
	REQUIRE(*evaluation.child(map, 1).failure(body) == "ThrowAt: element holding 2");
	REQUIRE_FALSE(evaluation.child(map, 0).hasFailure());
	REQUIRE(evaluation.child(map, 1).hasFailure());
}
