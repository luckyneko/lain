// A node that FAILS WITHOUT THROWING (NodeEvaluation::fail; ADR-0025, amended 2026-10-02): what a node
// does when it cannot produce its outputs and can say why — no backend in this build, inputs that
// disagree — so the reason reaches the failure record a host shows, rather than only a log line a gui
// user never sees. A board render in a build with no renderer was exactly that: an empty node on the
// canvas, and the reason on stderr.
//
// What each case pins:
//   * the reason is recorded against the node, every output is cleared WHATEVER THE BODY WROTE, and
//     downstream is suppressed (ADR-0007) — while the run carries on, as it does not for a throw;
//   * the node is COMPUTED, not owed: its failure is kept until its inputs or its recipe change, and
//     it is computed once per run however many stages the run has — each stage plans from the whole
//     stale closure, so a failure left stale like a throw would be computed again in every one;
//   * the record is cleared by the node's next compute that does not fail;
//   * a watching host is handed the record carrying the reason;
//   * failed() counts throws only (a host tells a throw some node owned from one none did by it),
//     while finished() counts this;
//   * a node that gave up on a cancel has given up, whatever it said after.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/runcontrol.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"
#include "testobserver.h"
#include "testscene.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

using namespace lain::flow;
using namespace lain::flow::test;

namespace lain::flow::test::fail
{
	// An int passthrough that, while `refusing`, writes its output ANYWAY and then fails — so a case
	// can tell that the scheduler cleared what the body wrote. `refusing` is held by reference, so a
	// test changes it without a version bump: what makes the node compute again has to be an edit.
	struct Refuser : Node
	{
		std::atomic<int>& calls;
		bool& refusing;
		std::string reason;
		PortId in, out;
		Refuser(std::atomic<int>& c, bool& r, std::string why)
			: Node("Refuser")
			, calls(c)
			, refusing(r)
			, reason(std::move(why))
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Refuser>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
			if (refusing)
				evaluation.fail(reason);
		}
	};

	// Cancels the run computing it, ASKS — and hears yes — then fails anyway: a long compute that
	// polls cancelled(), returns early, and says why on the way out.
	struct GiveUpAndFail : Node
	{
		RunControl*& trigger;
		PortId in, out;
		explicit GiveUpAndFail(RunControl*& t)
			: Node("GiveUpAndFail")
			, trigger(t)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<GiveUpAndFail>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			trigger->cancel();
			if (evaluation.cancelled())
				evaluation.fail("stopped part-way");
		}
	};

	// source -> refuser -> after, added to `graph` (which may already hold a scene).
	struct Refusing
	{
		std::atomic<int> sourceCalls{0}, refuserCalls{0}, afterCalls{0};
		bool refusing = true;
		NodeId source, refuser, after;

		Refusing(Graph& graph, std::string reason)
		{
			source = graph.add<Source>(sourceCalls, 1);
			refuser = graph.add<Refuser>(refuserCalls, refusing, std::move(reason));
			after = graph.add<Relay>(afterCalls);
			REQUIRE(graph.connect(source, 0, refuser, 0) == Connection::Ok);
			REQUIRE(graph.connect(refuser, 0, after, 0) == Connection::Ok);
		}
		Refusing(const Refusing&) = delete;

		void setSource(Graph& graph, int value) const
		{
			auto& node = static_cast<Source&>(graph.node(source));
			REQUIRE(node.setParam(node.value, value));
		}
	};
} // namespace lain::flow::test::fail

using namespace lain::flow::test::fail;

static void checkFailRecorded(Scheduler& scheduler)
{
	Graph graph;
	Refusing chain(graph, "this build has no backend for it");
	Evaluation evaluation{graph};
	RunControl control;
	scheduler.run(graph, evaluation, control); // does not throw: the run carries on

	const std::string* failure = evaluation.failure(chain.refuser);
	REQUIRE(failure != nullptr);
	REQUIRE(*failure == "this build has no backend for it");
	REQUIRE(evaluation.hasFailure());

	// Cleared whatever the body wrote, so downstream is suppressed rather than run on a value the
	// node itself disowned.
	REQUIRE(output(graph, evaluation, chain.refuser, 0).empty());
	REQUIRE(chain.afterCalls == 0);
	REQUIRE_FALSE(evaluation.ready(chain.after));
	REQUIRE(evaluation.failure(chain.after) == nullptr); // suppressed, which is not failing

	// Computed, not owed — and counted as finished, never as failed(), which counts throws.
	REQUIRE_FALSE(evaluation.needsRecompute(chain.refuser));
	REQUIRE(control.failed() == 0);
	REQUIRE(control.finished() == control.planned());

	// Cleared by the next compute that does not fail. Disarming is no edit, so make one.
	chain.refusing = false;
	chain.setSource(graph, 2);
	scheduler.run(graph, evaluation);
	REQUIRE(evaluation.failure(chain.refuser) == nullptr);
	REQUIRE_FALSE(evaluation.hasFailure());
	REQUIRE(intOut(graph, evaluation, chain.after) == 2);
}

TEST_CASE("a node that fails without throwing records why and produces nothing", "[flow][failure]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkFailRecorded(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkFailRecorded(scheduler);
	}
}

TEST_CASE("a failure that does not throw is kept until the node's inputs change", "[flow][failure]")
{
	Graph graph;
	Refusing chain(graph, "inputs disagree");
	Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation);
	REQUIRE(chain.refuserCalls == 1);

	// Nothing changed, so nothing is asked again: the failure is this node's answer to these inputs.
	scheduler.run(graph, evaluation);
	REQUIRE(chain.refuserCalls == 1);
	REQUIRE(evaluation.failure(chain.refuser) != nullptr);

	// An input changed: asked again, and it still fails.
	chain.setSource(graph, 2);
	scheduler.run(graph, evaluation);
	REQUIRE(chain.refuserCalls == 2);
	REQUIRE(evaluation.failure(chain.refuser) != nullptr);
	REQUIRE(chain.afterCalls == 0);
}

TEST_CASE("a failure that does not throw is computed once per run, not once per stage", "[flow][failure][map]")
{
	// A map raises a frontier, so this run has two stages, and the second plans from the whole stale
	// closure. A failure left stale (as a throw's is) would be computed again there.
	Calls calls;
	Graph graph;
	buildScene(graph, calls);
	Refusing chain(graph, "no backend");

	Evaluation evaluation{graph};
	Recorder recorder;
	RunControl control;
	control.setObserver(&recorder);
	SerialScheduler{}.run(graph, evaluation, control);
	REQUIRE(recorder.count(Report::Kind::Stage, EvalPath{}, NodeId{}) >= 1); // more than one stage
	REQUIRE(calls.element == 3);
	REQUIRE(chain.refuserCalls == 1);
	REQUIRE(evaluation.failure(chain.refuser) != nullptr);
}

TEST_CASE("a failure that does not throw is reported with its reason", "[flow][failure][observer]")
{
	Graph graph;
	Refusing chain(graph, "no renderer");
	Evaluation evaluation{graph};
	Recorder recorder;
	RunControl control;
	control.setObserver(&recorder);
	SerialScheduler{}.run(graph, evaluation, control);

	// The record a host folds into the copy its panes read, so the reason shows the moment it lands.
	const NodeRecord* record = recorder.lastRecord(EvalPath{}, chain.refuser);
	REQUIRE(record != nullptr);
	REQUIRE(record->failure() != nullptr);
	REQUIRE(*record->failure() == "no renderer");
	REQUIRE(recorder.count(Report::Kind::GaveUp, EvalPath{}, chain.refuser) == 0);
}

TEST_CASE("a failure that does not throw always says something", "[flow][failure]")
{
	Graph graph;
	Refusing chain(graph, "");
	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	const std::string* failure = evaluation.failure(chain.refuser);
	REQUIRE(failure != nullptr);
	REQUIRE_FALSE(failure->empty()); // a failure a host cannot describe reads as none at all
}

TEST_CASE("a node that gave up on a cancel records no failure, whatever it said", "[flow][failure][cancel]")
{
	Graph graph;
	std::atomic<int> sourceCalls{0};
	RunControl* trigger = nullptr;
	const NodeId source = graph.add<Source>(sourceCalls, 1);
	const NodeId bailer = graph.add<GiveUpAndFail>(trigger);
	REQUIRE(graph.connect(source, 0, bailer, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	RunControl control;
	trigger = &control;
	SerialScheduler{}.run(graph, evaluation, control);
	REQUIRE(control.cancelled());
	REQUIRE(evaluation.failure(bailer) == nullptr);
	REQUIRE(evaluation.needsRecompute(bailer)); // still owed, as a give-up always is
}
