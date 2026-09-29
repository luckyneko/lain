// The pull path (Scheduler::evaluate) AFTER AN EDIT.
//
// Every other pull test starts from a fresh evaluation, where everything is stale — and there the
// nodes of a cone that are stale on their own account and the part of the stale closure in that cone
// are the same set. They part company only once something has been computed and then edited, which
// is exactly where the pull used to go wrong: it recomputed the edited node, left what that node
// feeds holding values built from the old input, and — the edited node's record now clean — let the
// stale closure call all of it current.
//
// So each case here runs once, edits, and then pulls. What each pins:
//   * a pull recomputes its target when something upstream of it was edited;
//   * a pull leaves what it did not reach STALE, including a node it fed the new value past;
//   * a group outside the cone that the edit feeds, and that is stale inside too, republishes next
//     run — its own record being clean is not the same as its interior having seen the new input;
//   * a group outside the cone that is stale ONLY inside does not republish — a request would throw
//     away its inner incrementality, which is the rule the full run's closure already follows.
//
// And the shape of a pull as a gui host's Run Selection uses it (M14 slice 8): several targets at once
// compute each cone once and nothing else, under either strategy; a target the definition does not
// hold contributes nothing, and no targets at all is a pull of nothing.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/runcontrol.h"
#include "lain/flow/scheduler.h"
#include "lain/flow/staleness.h"
#include "testscene.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <vector>

using namespace lain::flow;
using namespace lain::flow::test;

namespace
{
	void setSource(Graph& graph, NodeId id, int value)
	{
		auto& source = static_cast<Source&>(graph.node(id));
		REQUIRE(source.setParam(source.value, value));
	}

	// Two root sources and a group:
	//
	//   u ─┬─> [group: x -> relay ─┐          ] -> (group's own output)
	//      │   [       inner ──────┴─> add -> y]
	//      └─> t
	//   w ───> t2
	//
	// `t`'s cone is {u, t} and `t2`'s is {w, t2}, so the group is outside both. Inside, `relay` is fed
	// by the boundary and `inner` is a source of its own, so an edit to `inner` leaves `relay` current
	// unless the group republishes.
	struct GroupScene
	{
		std::atomic<int> uCalls{0};
		std::atomic<int> wCalls{0};
		std::atomic<int> tCalls{0};
		std::atomic<int> t2Calls{0};
		std::atomic<int> relayCalls{0};
		std::atomic<int> innerCalls{0};

		Graph graph;
		NodeId u, w, t, t2, group, relay, inner;

		GroupScene()
		{
			registerSceneTypes();
			u = graph.add<Source>(uCalls, 1);
			w = graph.add<Source>(wCalls, 100);
			t = graph.add<Relay>(tCalls);
			t2 = graph.add<Relay>(t2Calls);
			REQUIRE(graph.connect(u, 0, t, 0) == Connection::Ok);
			REQUIRE(graph.connect(w, 0, t2, 0) == Connection::Ok);

			group = graph.add<InlineGroupNode>();
			Graph& in = interior();
			const PortId x = in.boundaryInputNode().addBoundary<int>("x");
			const PortId y = in.boundaryOutputNode().addBoundary<int>("y");
			relay = in.add<Relay>(relayCalls);
			inner = in.add<Source>(innerCalls, 10);
			const NodeId add = in.add<AddInt>();
			REQUIRE(in.connect(PortAddress{in.boundaryInputNode().id(), x}, PortAddress{relay, in.node(relay).input(0).id()}) ==
					Connection::Ok);
			REQUIRE(in.connect(relay, 0, add, 0) == Connection::Ok);
			REQUIRE(in.connect(inner, 0, add, 1) == Connection::Ok);
			REQUIRE(in.connect(PortAddress{add, in.node(add).output(0).id()}, PortAddress{in.boundaryOutputNode().id(), y}) ==
					Connection::Ok);
			edit::syncGroupPorts(graph, group);
			REQUIRE(graph.connect(u, 0, group, 0) == Connection::Ok);
		}

		Graph& interior() { return static_cast<InlineGroupNode&>(graph.node(group)).inner(); }

		void resetCalls()
		{
			for (std::atomic<int>* calls : {&uCalls, &wCalls, &tCalls, &t2Calls, &relayCalls, &innerCalls})
				*calls = 0;
		}
	};
} // namespace

TEST_CASE("a pull recomputes a target whose upstream was edited", "[scheduler][pull]")
{
	// u -> d -> t, then u edited. The target is stale only because of what feeds it — and that is the
	// case the pull used to leave alone, with t still showing 1 and reading as current.
	Calls calls;
	std::atomic<int> dCalls{0};
	Graph graph;
	const NodeId u = graph.add<Source>(calls.source, 1);
	const NodeId d = graph.add<Relay>(dCalls);
	const NodeId t = graph.add<Relay>(calls.sink);
	REQUIRE(graph.connect(u, 0, d, 0) == Connection::Ok);
	REQUIRE(graph.connect(d, 0, t, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation);
	REQUIRE(intOut(graph, evaluation, t) == 1);

	setSource(graph, u, 2);
	dCalls = 0;
	calls.sink = 0;
	scheduler.evaluate(graph, evaluation, t);

	REQUIRE(intOut(graph, evaluation, t) == 2);
	REQUIRE(dCalls == 1);
	REQUIRE(calls.sink == 1);
	REQUIRE(StaleClosure(graph, evaluation).order().empty());
}

TEST_CASE("a pull leaves what it did not reach stale", "[scheduler][pull]")
{
	// u -> t and u -> x, then u edited and t pulled. x is outside the cone, so it is not computed —
	// but it was stale only because of u, which the pull has just made clean. It has to stay stale on
	// its own account, or it reads as current while still showing u's old value.
	Calls calls;
	std::atomic<int> xCalls{0};
	Graph graph;
	const NodeId u = graph.add<Source>(calls.source, 1);
	const NodeId t = graph.add<Relay>(calls.sink);
	const NodeId x = graph.add<Relay>(xCalls);
	REQUIRE(graph.connect(u, 0, t, 0) == Connection::Ok);
	REQUIRE(graph.connect(u, 0, x, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation);

	setSource(graph, u, 2);
	xCalls = 0;
	scheduler.evaluate(graph, evaluation, t);

	REQUIRE(intOut(graph, evaluation, t) == 2);
	REQUIRE(xCalls == 0);								  // not the pull's business
	REQUIRE(intOut(graph, evaluation, x) == 1);			  // so it still shows the old value
	REQUIRE(StaleClosure(graph, evaluation).contains(x)); // and says so
	REQUIRE_FALSE(StaleClosure(graph, evaluation).contains(t));

	scheduler.run(graph, evaluation);
	REQUIRE(xCalls == 1);
	REQUIRE(intOut(graph, evaluation, x) == 2);
}

TEST_CASE("a group outside the cone that the edit feeds republishes next run", "[scheduler][pull][group]")
{
	// u edited AND the group's inner source edited, then t pulled. The group is outside t's cone and
	// stale on its own account (for its interior) — but it is also fed the new u, which the pull makes
	// clean. Without a request of its own it would not republish next run, and its interior would add
	// the new inner value to the OLD u: 21 rather than 22.
	GroupScene scene;
	Evaluation evaluation{scene.graph};
	SerialScheduler scheduler;
	scheduler.run(scene.graph, evaluation);
	REQUIRE(intOut(scene.graph, evaluation, scene.group) == 11);

	setSource(scene.graph, scene.u, 2);
	setSource(scene.interior(), scene.inner, 20);
	scheduler.evaluate(scene.graph, evaluation, scene.t);

	REQUIRE(intOut(scene.graph, evaluation, scene.t) == 2);
	REQUIRE(intOut(scene.graph, evaluation, scene.group) == 11); // outside the cone: untouched
	REQUIRE(StaleClosure(scene.graph, evaluation).contains(scene.group));

	scheduler.run(scene.graph, evaluation);
	REQUIRE(intOut(scene.graph, evaluation, scene.group) == 22);
}

TEST_CASE("a group outside the cone that is stale only inside does not republish", "[scheduler][pull][group]")
{
	// w and the group's inner source edited, then t2 pulled. The group is outside t2's cone and nothing
	// the pull computes feeds it: it is stale only for its interior. A request on it would make it
	// republish next run and recompute the relay its boundary feeds — the inner incrementality the
	// full run's own closure is careful not to throw away.
	GroupScene scene;
	Evaluation evaluation{scene.graph};
	SerialScheduler scheduler;
	scheduler.run(scene.graph, evaluation);

	setSource(scene.graph, scene.w, 200);
	setSource(scene.interior(), scene.inner, 20);
	scheduler.evaluate(scene.graph, evaluation, scene.t2);
	REQUIRE(intOut(scene.graph, evaluation, scene.t2) == 200);
	REQUIRE(StaleClosure(scene.graph, evaluation).contains(scene.group)); // still owed for its interior

	scene.resetCalls();
	scheduler.run(scene.graph, evaluation);
	REQUIRE(intOut(scene.graph, evaluation, scene.group) == 21);
	REQUIRE(scene.innerCalls == 1);
	REQUIRE(scene.relayCalls == 0); // the boundary handed it nothing new
}

//=========================================================================
// Many targets — a host's Run Selection
//=========================================================================

static void checkSeveralTargets(Scheduler& scheduler)
{
	// shared -> a, shared -> b, and beside them other -> c. Pull {a, b}: their cones overlap in
	// `shared`, which computes ONCE, and `other` and `c` are nobody's business.
	std::atomic<int> sharedCalls{0}, aCalls{0}, bCalls{0}, otherCalls{0}, cCalls{0};
	Graph graph;
	const NodeId shared = graph.add<Source>(sharedCalls, 5);
	const NodeId a = graph.add<Relay>(aCalls);
	const NodeId b = graph.add<Relay>(bCalls);
	const NodeId other = graph.add<Source>(otherCalls, 9);
	const NodeId c = graph.add<Relay>(cCalls);
	REQUIRE(graph.connect(shared, 0, a, 0) == Connection::Ok);
	REQUIRE(graph.connect(shared, 0, b, 0) == Connection::Ok);
	REQUIRE(graph.connect(other, 0, c, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	RunControl control;
	scheduler.evaluate(graph, evaluation, std::vector<NodeId>{a, b}, control);

	REQUIRE(sharedCalls == 1);
	REQUIRE(aCalls == 1);
	REQUIRE(bCalls == 1);
	REQUIRE(otherCalls == 0);
	REQUIRE(cCalls == 0);
	REQUIRE(intOut(graph, evaluation, a) == 5);
	REQUIRE(intOut(graph, evaluation, b) == 5);
	REQUIRE(control.planned() == 3);
	REQUIRE(control.finished() == 3);

	const StaleClosure closure(graph, evaluation);
	REQUIRE(closure.contains(other));
	REQUIRE(closure.contains(c));
	REQUIRE_FALSE(closure.contains(a));
}

TEST_CASE("a pull of several targets computes each cone once and nothing else", "[scheduler][pull]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkSeveralTargets(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkSeveralTargets(scheduler);
	}
}

TEST_CASE("a target the definition does not hold contributes nothing", "[scheduler][pull]")
{
	// A host's selection can outlive a node: captured when Run Selection is chosen, it may name a node
	// deleted before the run starts. Such a target is in no edge and no closure, so it adds nothing —
	// and does not stop the rest of the selection from running.
	std::atomic<int> uCalls{0}, tCalls{0};
	Graph graph;
	const NodeId u = graph.add<Source>(uCalls, 3);
	const NodeId t = graph.add<Relay>(tCalls);
	REQUIRE(graph.connect(u, 0, t, 0) == Connection::Ok);

	Graph elsewhere;
	const NodeId stranger = elsewhere.add<Relay>(tCalls);
	REQUIRE_FALSE(graph.contains(stranger));

	Evaluation evaluation{graph};
	RunControl control;
	SerialScheduler scheduler;

	SECTION("alone, it pulls nothing")
	{
		scheduler.evaluate(graph, evaluation, std::vector<NodeId>{stranger}, control);
		REQUIRE(uCalls == 0);
		REQUIRE(tCalls == 0);
		REQUIRE(control.planned() == 0);
	}
	SECTION("beside a real target, the real one still runs")
	{
		scheduler.evaluate(graph, evaluation, std::vector<NodeId>{stranger, t}, control);
		REQUIRE(uCalls == 1);
		REQUIRE(tCalls == 1);
		REQUIRE(intOut(graph, evaluation, t) == 3);
	}
}

TEST_CASE("a pull of no targets computes nothing", "[scheduler][pull]")
{
	std::atomic<int> uCalls{0};
	Graph graph;
	const NodeId u = graph.add<Source>(uCalls, 3);

	Evaluation evaluation{graph};
	RunControl control;
	SerialScheduler{}.evaluate(graph, evaluation, std::vector<NodeId>{}, control);

	REQUIRE(uCalls == 0);
	REQUIRE(StaleClosure(graph, evaluation).contains(u));
}
