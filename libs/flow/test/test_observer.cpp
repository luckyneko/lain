// Watching a run (M14 slice 7, ADR-0025): a RunObserver is told which nodes each stage owes, when each
// compute starts, and each step's result the moment it finishes — WHERE, by EvalPath, since the
// working evaluation's address names nothing in the copy a host reads. A host folds those reports into
// a PublishedEvaluation, so a node's result shows as it lands rather than when the run ends.
//
// What each case pins:
//   * every compute reports started then finished, at the path it ran at — the root, a group's
//     interior {group, 0}, a map's element {map, i}, a loop's interior {loop, 0};
//   * a stage reports every node it owes before any of its computes start;
//   * a crossing reports finished for the node that owns it, and never started;
//   * a copy taken at run start, with every report replayed onto it in order, SHOWS exactly what a
//     copy taken at run end shows — for a group, a map that grew, and a loop;
//   * the owed marks are what keep that sound part-way through: a node downstream of one that has
//     landed reads as stale until it lands itself — and, without the marks, would not;
//   * fold and owe refuse what the copy does not have;
//   * a PULL reports what it leaves owed apart from what it owes, and with those replayed too its
//     copy both matches its end and says what it left stale (M14 slice 8).
// What a cancel, a give-up and a throw report lives with the cancel suite (test_cancel.cpp,
// [observer]), beside the nodes that provoke them.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/nodes/constant.h"
#include "lain/flow/runcontrol.h"
#include "lain/flow/scheduler.h"
#include "lain/flow/staleness.h"
#include "testnodes.h"
#include "testobserver.h"
#include "testscene.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

using namespace lain::flow;
using namespace lain::flow::test;

namespace lain::flow::test::observer
{
	// The one node inside `owner`'s interior that is not a boundary node — the body buildInterior put
	// there.
	static NodeId bodyOf(const Graph& graph, NodeId owner)
	{
		const Graph& inner = *graph.node(owner).innerGraph();
		for (const NodeId id : inner.nodeIds())
		{
			if (id != inner.boundaryInputNode().id() && id != inner.boundaryOutputNode().id())
				return id;
		}
		FAIL("no body in that interior");
		return NodeId{};
	}

	static PortId inputNamed(const Node& node, const std::string& name)
	{
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			if (node.input(i).name() == name)
				return node.input(i).id();
		}
		return PortId{};
	}

	static PortId outputNamed(const Node& node, const std::string& name)
	{
		for (std::size_t o = 0; o < node.outputCount(); ++o)
		{
			if (node.output(o).name() == name)
				return node.output(o).id();
		}
		return PortId{};
	}

	// int -> int + 1, counted: the body of a fold.
	struct PlusOne : Node
	{
		std::atomic<int>& calls;
		PortId in, out;
		explicit PlusOne(std::atomic<int>& c)
			: Node("PlusOne")
			, calls(c)
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<PlusOne>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>() + 1);
		}
	};

	// seed -> loop(value: value + 1, `trips` times) -> sink. The seed is a constant, so a test edits it
	// as a recipe change.
	struct LoopScene
	{
		Graph graph;
		std::atomic<int> bodyCalls{0};
		std::atomic<int> sinkCalls{0};
		NodeId seed, loop, body, sink;

		explicit LoopScene(int trips)
		{
			seed = graph.add(constantOf(1));
			const NodeId count = graph.add(constantOf(trips));
			loop = graph.add<LoopNode>();
			auto& node = static_cast<LoopNode&>(graph.node(loop));
			const LoopNode::Carry carry = node.addCarry<int>("value");
			Graph& inner = node.inner();
			body = inner.add<PlusOne>(bodyCalls);
			REQUIRE(inner.connect(PortAddress{inner.boundaryInputNode().id(), carry.innerIn},
								  PortAddress{body, inner.node(body).input(0).id()}) == Connection::Ok);
			REQUIRE(inner.connect(PortAddress{body, inner.node(body).output(0).id()},
								  PortAddress{inner.boundaryOutputNode().id(), carry.innerOut}) == Connection::Ok);
			edit::syncGroupPorts(graph, loop);

			REQUIRE(graph.connect(PortAddress{seed, graph.node(seed).output(0).id()},
								  PortAddress{loop, inputNamed(graph.node(loop), "value")}) == Connection::Ok);
			REQUIRE(graph.connect(PortAddress{count, graph.node(count).output(0).id()},
								  PortAddress{loop, node.countPort()}) == Connection::Ok);
			// By NAME: a loop's first output is its own `iterations`, not the carry.
			sink = graph.add<Relay>(sinkCalls);
			REQUIRE(graph.connect(PortAddress{loop, outputNamed(graph.node(loop), "value")},
								  PortAddress{sink, graph.node(sink).input(0).id()}) == Connection::Ok);
		}
		LoopScene(const LoopScene&) = delete;

		void setSeed(int value) { REQUIRE(static_cast<ConstantNode&>(graph.node(seed)).setValue(value)); }
	};

	// What a gui host does around one run: clone the document, prepare the working evaluation against
	// the clone, publish at the START (what reports fold into), run watched, publish at the END. Given
	// `targets`, the run is a PULL of them (Scheduler::evaluate) — a host's Run Selection.
	struct WatchedRun
	{
		std::shared_ptr<const Graph> clone;
		PublishedEvaluation start;
		PublishedEvaluation end;
		Recorder recorder;
	};

	static void watch(Scheduler& scheduler, const Graph& document, Evaluation& working, WatchedRun& run,
					  const std::vector<NodeId>* targets = nullptr)
	{
		run.clone = std::make_shared<const Graph>(document.clone());
		working.prepare(*run.clone);
		run.start = PublishedEvaluation{run.clone, working};
		RunControl control;
		control.setObserver(&run.recorder);
		if (targets != nullptr)
			scheduler.evaluate(*run.clone, working, *targets, control);
		else
			scheduler.run(*run.clone, working, control);
		run.end = PublishedEvaluation{run.clone, working};
	}
} // namespace lain::flow::test::observer

using namespace lain::flow::test::observer;

//=========================================================================
// What a run reports, and where
//=========================================================================

static void checkComputesReported(Scheduler& scheduler)
{
	Calls calls;
	Graph graph;
	const Scene s = buildScene(graph, calls);
	const NodeId groupBody = bodyOf(graph, s.group);
	const NodeId mapBody = bodyOf(graph, s.map);

	Evaluation evaluation{graph};
	Recorder recorder;
	RunControl control;
	control.setObserver(&recorder);
	scheduler.run(graph, evaluation, control);

	// Every compute: started, then finished with a record — at the path it ran at.
	const auto startedThenFinished = [&](const EvalPath& path, NodeId node)
	{
		REQUIRE(recorder.count(Report::Kind::Started, path, node) == 1);
		REQUIRE(recorder.count(Report::Kind::Finished, path, node) == 1);
		REQUIRE(recorder.first(Report::Kind::Started, path, node) < recorder.first(Report::Kind::Finished, path, node));
	};
	startedThenFinished(EvalPath{}, s.source);
	startedThenFinished(EvalPath{}, s.sink);
	startedThenFinished(EvalPath{}, s.list);
	startedThenFinished(EvalPath{{s.group, 0}}, groupBody);
	for (std::size_t element = 0; element < 3; ++element)
		startedThenFinished(EvalPath{{s.map, element}}, mapBody);

	// Nothing is reported at a coordinate the run did not use: the group has no element 1.
	REQUIRE(recorder.count(Report::Kind::Started, EvalPath{{s.group, 1}}, groupBody) == 0);
	REQUIRE(calls.element == 3);
}

TEST_CASE("every compute reports started then finished at the path it ran at", "[flow][observer]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkComputesReported(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkComputesReported(scheduler);
	}
}

static void checkLoopReported(Scheduler& scheduler)
{
	LoopScene scene{3};
	Evaluation evaluation{scene.graph};
	Recorder recorder;
	RunControl control;
	control.setObserver(&recorder);
	scheduler.run(scene.graph, evaluation, control);

	// One interior, reused: every iteration's body reports at {loop, 0}, once per pass.
	REQUIRE(scene.bodyCalls == 3);
	REQUIRE(recorder.count(Report::Kind::Started, EvalPath{{scene.loop, 0}}, scene.body) == 3);
	REQUIRE(recorder.count(Report::Kind::Finished, EvalPath{{scene.loop, 0}}, scene.body) == 3);
	// The loop is owed by every stage it iterates in, and never shown computing — its fold is a
	// crossing.
	REQUIRE(recorder.count(Report::Kind::Owed, EvalPath{}, scene.loop) >= 3);
	REQUIRE(recorder.count(Report::Kind::Started, EvalPath{}, scene.loop) == 0);
	REQUIRE(recorder.count(Report::Kind::Finished, EvalPath{}, scene.loop) == 1);
	REQUIRE(intOut(scene.graph, evaluation, scene.sink) == 4);
}

TEST_CASE("a loop reports each pass at its one interior", "[flow][observer][loop]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkLoopReported(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkLoopReported(scheduler);
	}
}

static void checkOwedFirst(Scheduler& scheduler)
{
	Calls calls;
	Graph graph;
	buildScene(graph, calls);
	Evaluation evaluation{graph};
	Recorder recorder;
	RunControl control;
	control.setObserver(&recorder);
	scheduler.run(graph, evaluation, control);

	// Split the reports at each stage boundary. Within a stage, every compute was owed first, and
	// every owed report came before the stage's first compute: the closure is persisted — and
	// reported — before anything in the stage runs.
	std::size_t begin = 0;
	int stages = 0;
	for (std::size_t i = 0; i <= recorder.reports.size(); ++i)
	{
		if (i < recorder.reports.size() && recorder.reports[i].kind != Report::Kind::Stage)
			continue;
		++stages;
		std::size_t lastOwed = begin;
		std::size_t firstStarted = i;
		for (std::size_t r = begin; r < i; ++r)
		{
			const Report& report = recorder.reports[r];
			if (report.kind == Report::Kind::Owed)
				lastOwed = r;
			if (report.kind == Report::Kind::Started && r < firstStarted)
			{
				firstStarted = r;
			}
			if (report.kind == Report::Kind::Started)
			{
				bool owedHere = false;
				for (std::size_t o = begin; o < r; ++o)
				{
					const Report& earlier = recorder.reports[o];
					if (earlier.kind == Report::Kind::Owed && earlier.path == report.path && earlier.node == report.node)
						owedHere = true;
				}
				REQUIRE(owedHere);
			}
		}
		REQUIRE(lastOwed < firstStarted);
		begin = i + 1;
	}
	REQUIRE(stages == 2); // the root and the list, then the map's elements
}

TEST_CASE("a stage reports every node it owes before any of its computes start", "[flow][observer][map]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkOwedFirst(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkOwedFirst(scheduler);
	}
}

TEST_CASE("a crossing reports finished for its owner and never started", "[flow][observer][group][map]")
{
	Calls calls;
	Graph graph;
	const Scene s = buildScene(graph, calls);
	Evaluation evaluation{graph};
	Recorder recorder;
	RunControl control;
	control.setObserver(&recorder);
	SerialScheduler{}.run(graph, evaluation, control);

	// A group crosses twice — its entry and its exit — and a map once, at its gather.
	REQUIRE(recorder.count(Report::Kind::Finished, EvalPath{}, s.group) == 2);
	REQUIRE(recorder.count(Report::Kind::Finished, EvalPath{}, s.map) == 1);
	REQUIRE(recorder.count(Report::Kind::Started, EvalPath{}, s.group) == 0);
	REQUIRE(recorder.count(Report::Kind::Started, EvalPath{}, s.map) == 0);

	// And what the gather reports IS the map as it published: folded into a copy taken before the run,
	// the map's output becomes the collection the run ended with.
	Graph fresh;
	Calls freshCalls;
	const Scene f = buildScene(fresh, freshCalls);
	Evaluation working{fresh};
	auto clone = std::make_shared<const Graph>(fresh.clone());
	working.prepare(*clone);
	PublishedEvaluation before{clone, working};
	Recorder watched;
	RunControl watchedControl;
	watchedControl.setObserver(&watched);
	SerialScheduler{}.run(*clone, working, watchedControl);

	const NodeRecord* gather = watched.lastRecord(EvalPath{}, f.map);
	REQUIRE(gather != nullptr);
	REQUIRE(gather->failure() == nullptr);
	REQUIRE_FALSE(before.evaluation().hasValue(PortAddress{f.map, clone->node(f.map).output(0).id()}));
	REQUIRE(before.fold(EvalPath{}, f.map, *gather));
	REQUIRE(output(*clone, before.evaluation(), f.map, 0).get<Ints>() == Ints{1, 2, 3});
}

//=========================================================================
// What a host can rebuild from the reports alone
//=========================================================================

static void checkFoldIsExact(Scheduler& scheduler)
{
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	Evaluation working{document};

	// A first run, so the second is incremental: what it does NOT recompute stays as the start copy
	// has it, and only what the reports carry changes.
	{
		WatchedRun first;
		watch(scheduler, document, working, first);
	}

	// Edit both chains: the source (so the group republishes and its interior recomputes) and the
	// list, GROWN, so the map gains an element the start copy does not have — a fold must create it.
	auto& source = static_cast<Source&>(document.node(s.source));
	REQUIRE(source.setParam(source.value, 7));
	auto& list = static_cast<MakeInts&>(document.node(s.list));
	REQUIRE(list.setParam(list.values, Ints{1, 2, 3, 4}));

	WatchedRun run;
	watch(scheduler, document, working, run);
	REQUIRE(run.start.evaluation().childCount(s.map) == 3);
	REQUIRE(run.end.evaluation().childCount(s.map) == 4);

	replay(run.start, run.recorder.reports);
	requireSameShown(*run.clone, run.start.evaluation(), run.end.evaluation());

	// And asked about the document, both are equally current: nothing is left looking stale that the
	// run finished, and nothing looks current that it did not.
	const StaleClosure fromReports(document, run.start.evaluation());
	REQUIRE(fromReports.order().empty());
	REQUIRE(intOut(*run.clone, run.start.evaluation(), s.sink) == 7);
}

TEST_CASE("a copy taken at run start and replayed shows what the run ended with", "[flow][observer][published][map]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkFoldIsExact(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkFoldIsExact(scheduler);
	}
}

static void checkLoopFoldIsExact(Scheduler& scheduler)
{
	LoopScene scene{3};
	Evaluation working{scene.graph};
	{
		WatchedRun first;
		watch(scheduler, scene.graph, working, first);
	}
	scene.setSeed(10);

	WatchedRun run;
	watch(scheduler, scene.graph, working, run);
	replay(run.start, run.recorder.reports);
	requireSameShown(*run.clone, run.start.evaluation(), run.end.evaluation());
	REQUIRE(intOut(*run.clone, run.start.evaluation(), scene.sink) == 13);
}

TEST_CASE("a loop's reports replayed show what its fold ended with", "[flow][observer][published][loop]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkLoopFoldIsExact(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkLoopFoldIsExact(scheduler);
	}
}

//=========================================================================
// A pull — a host's Run Selection (M14 slice 8)
//=========================================================================

// The scene run once, then the source edited and the list grown — both chains stale — ready for a
// pull that reaches only part of it.
static void runThenEditBoth(Scheduler& scheduler, Graph& document, const Scene& s, Evaluation& working)
{
	{
		WatchedRun first;
		watch(scheduler, document, working, first);
	}
	auto& source = static_cast<Source&>(document.node(s.source));
	REQUIRE(source.setParam(source.value, 7));
	auto& list = static_cast<MakeInts&>(document.node(s.list));
	REQUIRE(list.setParam(list.values, Ints{1, 2, 3, 4}));
}

TEST_CASE("a pull reports what it leaves owed, and never as owed", "[flow][observer][pull]")
{
	// Pull the SOURCE alone. The group and the sink are fed the new value past the cone and the map is
	// fed by the grown list: each has a clean record of its own, so each is LEFT owed — told to the
	// host so its copy marks them, and told apart from what the run owes, because the run will never
	// compute them. The list is stale on its own account, so it needs no word from this run at all.
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	SerialScheduler scheduler;
	Evaluation working{document};
	runThenEditBoth(scheduler, document, s, working);
	calls.inner = 0;
	calls.sink = 0;

	const std::vector<NodeId> targets{s.source};
	WatchedRun run;
	watch(scheduler, document, working, run, &targets);
	const Recorder& r = run.recorder;

	REQUIRE(r.count(Report::Kind::Owed, EvalPath{}, s.source) == 1);
	REQUIRE(r.count(Report::Kind::Finished, EvalPath{}, s.source) == 1);
	for (const NodeId left : {s.group, s.sink, s.map})
	{
		REQUIRE(r.count(Report::Kind::LeftOwed, EvalPath{}, left) == 1);
		REQUIRE(r.count(Report::Kind::Owed, EvalPath{}, left) == 0);
		REQUIRE(r.count(Report::Kind::Started, EvalPath{}, left) == 0);
		REQUIRE(r.count(Report::Kind::Finished, EvalPath{}, left) == 0);
	}
	REQUIRE(r.count(Report::Kind::LeftOwed, EvalPath{}, s.list) == 0);
	REQUIRE(r.count(Report::Kind::Owed, EvalPath{}, s.list) == 0);

	// Reported before anything started, as owed() is: a host has marked them before any result lands.
	REQUIRE(r.first(Report::Kind::LeftOwed, EvalPath{}, s.sink) < r.first(Report::Kind::Started, EvalPath{}, s.source));
	REQUIRE(calls.inner == 0);
	REQUIRE(calls.sink == 0);
}

static void checkPullFoldIsExact(Scheduler& scheduler, bool pullSink)
{
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	Evaluation working{document};
	runThenEditBoth(scheduler, document, s, working);

	// Pulling the sink takes the group's interior with it and leaves only the map; pulling the source
	// leaves the group and the sink too.
	const std::vector<NodeId> targets{pullSink ? s.sink : s.source};
	WatchedRun run;
	watch(scheduler, document, working, run, &targets);

	replay(run.start, run.recorder.reports);
	requireSameShown(*run.clone, run.start.evaluation(), run.end.evaluation());

	// And asked about the document, the copy says what the pull did and did not do: what it left is
	// stale, however clean its own record — which, without the left-owed marks, it would not be.
	const StaleClosure fromReports(document, run.start.evaluation());
	REQUIRE_FALSE(fromReports.contains(s.source));
	REQUIRE(fromReports.contains(s.list));
	REQUIRE(fromReports.contains(s.map));
	REQUIRE(fromReports.contains(s.group) == !pullSink);
	REQUIRE(fromReports.contains(s.sink) == !pullSink);
	REQUIRE(intOut(*run.clone, run.start.evaluation(), s.sink) == (pullSink ? 7 : 1));
}

TEST_CASE("a pull's reports replayed show what it ended with, and what it left stale", "[flow][observer][published][pull]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkPullFoldIsExact(scheduler, false);
		checkPullFoldIsExact(scheduler, true);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkPullFoldIsExact(scheduler, false);
		checkPullFoldIsExact(scheduler, true);
	}
}

TEST_CASE("a node downstream of one that has landed stays stale until it lands itself", "[flow][observer][published]")
{
	// source -> [group: relay] -> sink, with the source edited. Replay up to the moment the SOURCE
	// lands: its record says current, while every node it feeds still shows the value it built from
	// the old one — so they must still read as stale.
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	const NodeId relay = bodyOf(document, s.group);
	SerialScheduler scheduler;
	Evaluation working{document};
	{
		WatchedRun first;
		watch(scheduler, document, working, first);
	}
	auto& source = static_cast<Source&>(document.node(s.source));
	REQUIRE(source.setParam(source.value, 7));

	WatchedRun run;
	watch(scheduler, document, working, run);
	const std::size_t sourceLanded = run.recorder.first(Report::Kind::Finished, EvalPath{}, s.source);
	REQUIRE(sourceLanded < run.recorder.reports.size());
	REQUIRE(run.recorder.first(Report::Kind::Finished, EvalPath{}, s.sink) > sourceLanded);

	SECTION("with the owed marks, what it feeds is stale until its own record lands")
	{
		replay(run.start, run.recorder.reports, 0, sourceLanded + 1);
		const StaleClosure root(document, run.start.evaluation());
		REQUIRE_FALSE(root.contains(s.source));
		REQUIRE(root.contains(s.group));
		REQUIRE(root.contains(s.sink));
		const StaleClosure inside(*document.node(s.group).innerGraph(), run.start.evaluation().child(s.group),
								  root.reseeds(s.group));
		REQUIRE(inside.contains(relay));
		REQUIRE(intOut(*run.clone, run.start.evaluation(), s.sink) == 1); // what it still shows

		// ... and once everything has landed, nothing is.
		replay(run.start, run.recorder.reports, sourceLanded + 1, run.recorder.reports.size());
		REQUIRE(StaleClosure(document, run.start.evaluation()).order().empty());
		REQUIRE(intOut(*run.clone, run.start.evaluation(), s.sink) == 7);
	}

	SECTION("without them, it would read as current while showing the old value")
	{
		// The hazard the marks exist for, shown directly: fold the source's record alone.
		REQUIRE(run.start.fold(EvalPath{}, s.source, *run.recorder.lastRecord(EvalPath{}, s.source)));
		REQUIRE_FALSE(StaleClosure(document, run.start.evaluation()).contains(s.sink));
		REQUIRE(intOut(*run.clone, run.start.evaluation(), s.sink) == 1);
	}
}

TEST_CASE("fold and owe refuse what the copy does not have", "[flow][observer][published]")
{
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	const NodeId relay = bodyOf(document, s.group);
	Evaluation working{document};
	SerialScheduler scheduler;
	WatchedRun run;
	watch(scheduler, document, working, run);
	const NodeRecord& record = *run.recorder.lastRecord(EvalPath{{s.group, 0}}, relay);

	SECTION("nothing published at all")
	{
		PublishedEvaluation nothing;
		REQUIRE_FALSE(nothing.fold(EvalPath{{s.group, 0}}, relay, record));
		REQUIRE_FALSE(nothing.owe(EvalPath{}, s.source));
	}

	SECTION("a node, or a level, its definition does not have")
	{
		REQUIRE_FALSE(run.start.fold(EvalPath{}, NodeId::generate(), record));
		REQUIRE_FALSE(run.start.owe(EvalPath{}, NodeId::generate()));
		REQUIRE_FALSE(run.start.fold(EvalPath{{NodeId::generate(), 0}}, relay, record));
		REQUIRE_FALSE(run.start.fold(EvalPath{{s.source, 0}}, relay, record)); // a source has no interior
		REQUIRE_FALSE(run.start.fold(EvalPath{{s.group, 1}}, relay, record));  // a group has one child
	}

	SECTION("a record of another version history")
	{
		// The same node of an unrelated document: its versions mean nothing against this one's.
		Calls otherCalls;
		Graph other;
		const Scene o = buildScene(other, otherCalls);
		Evaluation otherWorking{other};
		WatchedRun otherRun;
		watch(scheduler, other, otherWorking, otherRun);
		const NodeRecord& foreign = *otherRun.recorder.lastRecord(EvalPath{}, o.source);
		REQUIRE_FALSE(run.start.fold(EvalPath{}, s.source, foreign));
	}

	SECTION("a map's missing element is created rather than refused")
	{
		REQUIRE(run.start.evaluation().childCount(s.map) == 0); // prepared, never sized
		REQUIRE(run.start.owe(EvalPath{{s.map, 4}}, bodyOf(document, s.map)));
		REQUIRE(run.start.evaluation().childCount(s.map) == 5);
		// Every element made on the way reads as never computed there.
		REQUIRE(StaleClosure(*document.node(s.map).innerGraph(), run.start.evaluation().child(s.map, 2))
					.contains(bodyOf(document, s.map)));
	}
}
