// The stale closure as a QUERY (M14 slice 6, ADR-0025): which nodes of one level show a value that
// does not reflect a definition. The scheduler plans from it and a gui host asks it to draw what is
// out of date, so it is stated once — and these cases pin the parts a host could not work out from a
// per-node comparison of its own:
//   * it asks about a definition OTHER than the one the evaluation was prepared against — the
//     document, while the evaluation was computed on a clone of it;
//   * a node with no record at all is stale (added since; nothing published yet);
//   * staleness crosses levels both ways: a group is stale when anything inside it is, and an
//     interior is stale when its owner hands it new values — which depends on the interior's kind;
//   * a map is asked in EVERY element;
//   * a definition of another lineage makes everything stale rather than throwing.
// The first case ties it to the run: the closure asked before a run is what that run computes.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/nodes/constant.h"
#include "lain/flow/scheduler.h"
#include "lain/flow/staleness.h"
#include "testscene.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <string>

using namespace lain::flow;
using namespace lain::flow::test;

namespace lain::flow::test::staleness
{
	// int + a param offset, counted: a body whose recipe a test can edit where it sits.
	struct Offset : Node
	{
		std::atomic<int>& calls;
		PortId offset, in, out;
		explicit Offset(std::atomic<int>& c)
			: Node("Offset")
			, calls(c)
		{
			offset = addParam<int>("offset", 10);
			in = addInput<int>("x");
			out = addOutput<int>("y");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Offset>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>() + param(offset).get<int>());
		}
	};

	// source -> [group: x -> relay -> offset -> y] -> sink. Two nodes inside, so an edit to the
	// SECOND can be shown to leave the first alone.
	struct Nested
	{
		Graph graph;
		std::atomic<int> sourceCalls{0}, relayCalls{0}, offsetCalls{0}, sinkCalls{0};
		NodeId source, group, relay, offset, sink;

		Nested()
		{
			source = graph.add<Source>(sourceCalls, 1);
			group = graph.add<InlineGroupNode>();
			Graph& in = inner();
			const PortId inPin = in.boundaryInputNode().addBoundary<int>("x");
			const PortId outPin = in.boundaryOutputNode().addBoundary<int>("y");
			relay = in.add<Relay>(relayCalls);
			offset = in.add<Offset>(offsetCalls);
			REQUIRE(in.connect(PortAddress{in.boundaryInputNode().id(), inPin}, PortAddress{relay, in.node(relay).input(0).id()}) == Connection::Ok);
			REQUIRE(in.connect(relay, 0, offset, 0) == Connection::Ok);
			REQUIRE(in.connect(PortAddress{offset, in.node(offset).output(0).id()}, PortAddress{in.boundaryOutputNode().id(), outPin}) == Connection::Ok);
			edit::syncGroupPorts(graph, group);
			sink = graph.add<Relay>(sinkCalls);
			REQUIRE(graph.connect(source, 0, group, 0) == Connection::Ok);
			REQUIRE(graph.connect(group, 0, sink, 0) == Connection::Ok);
		}
		Nested(const Nested&) = delete;

		Graph& inner() { return static_cast<InlineGroupNode&>(graph.node(group)).inner(); }

		void setSource(int value)
		{
			auto& node = static_cast<Source&>(graph.node(source));
			REQUIRE(node.setParam(node.value, value));
		}
		void setOffset(int value)
		{
			auto& node = static_cast<Offset&>(inner().node(offset));
			REQUIRE(node.setParam(node.offset, value));
		}
	};

	static PortId inputNamed(const Node& node, const std::string& name)
	{
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			if (node.input(i).name() == name)
				return node.input(i).id();
		}
		return PortId{};
	}
} // namespace lain::flow::test::staleness

using namespace lain::flow::test::staleness;

static void checkClosureIsTheRun(Scheduler& scheduler)
{
	Graph graph;
	Calls calls;
	const Scene s = buildScene(graph, calls);
	Evaluation evaluation{graph};
	scheduler.run(graph, evaluation);
	REQUIRE(StaleClosure(graph, evaluation).order().empty());

	auto& source = static_cast<Source&>(graph.node(s.source));
	REQUIRE(source.setParam(source.value, 2));

	// Asked BEFORE the run: the edited source, everything it feeds, and nothing on the other chain.
	const StaleClosure closure(graph, evaluation);
	REQUIRE(closure.contains(s.source));
	REQUIRE(closure.contains(s.group));
	REQUIRE(closure.contains(s.sink));
	REQUIRE_FALSE(closure.contains(s.list));
	REQUIRE_FALSE(closure.contains(s.map));

	// ... and the run computes exactly that.
	scheduler.run(graph, evaluation);
	REQUIRE(calls.source == 2);
	REQUIRE(calls.inner == 2);
	REQUIRE(calls.sink == 2);
	REQUIRE(calls.list == 1);
	REQUIRE(calls.element == 3);
	REQUIRE(StaleClosure(graph, evaluation).order().empty());
}

TEST_CASE("the closure asked before a run is what that run computes", "[flow][staleness]")
{
	SECTION("serial")
	{
		SerialScheduler scheduler;
		checkClosureIsTheRun(scheduler);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		ParallelScheduler scheduler;
		checkClosureIsTheRun(scheduler);
	}
}

TEST_CASE("the closure compares the document against a copy published from a clone", "[flow][staleness][published]")
{
	// What a gui host does: a run reads a CLONE, the panes read a published copy of what it computed,
	// and edits keep landing on the document. The copy's own reads go through the clone, so asking IT
	// whether a node needs recomputing says no whatever the document has done since — which is why
	// the query takes the definition to compare against.
	Graph graph;
	Calls calls;
	const Scene s = buildScene(graph, calls);
	Evaluation evaluation{graph};
	const auto clone = std::make_shared<const Graph>(graph.clone());
	SerialScheduler{}.run(*clone, evaluation);
	const PublishedEvaluation published{clone, evaluation};

	REQUIRE(StaleClosure(graph, published.evaluation()).order().empty());

	auto& source = static_cast<Source&>(graph.node(s.source));
	REQUIRE(source.setParam(source.value, 2));

	REQUIRE_FALSE(published.evaluation().needsRecompute(s.source)); // it reads the clone
	const StaleClosure closure(graph, published.evaluation());
	REQUIRE(closure.contains(s.source));
	REQUIRE(closure.contains(s.group));
	REQUIRE(closure.contains(s.sink));
	REQUIRE_FALSE(closure.contains(s.list));
	REQUIRE_FALSE(closure.contains(s.map));
}

TEST_CASE("a node with no record is stale", "[flow][staleness]")
{
	Graph graph;
	Calls calls;
	const Scene s = buildScene(graph, calls);
	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	SECTION("one added to the document since the run")
	{
		std::atomic<int> added{0};
		const NodeId fresh = graph.add<Source>(added, 7);
		const StaleClosure closure(graph, evaluation);
		REQUIRE(closure.contains(fresh));
		REQUIRE(closure.order().size() == 1); // and nothing else: nothing it feeds, nothing it touched
	}
	SECTION("every node, against an evaluation nothing has run in")
	{
		// What a host's empty published copy is between a document swap and its first run.
		const Evaluation nothing;
		const StaleClosure closure(graph, nothing);
		REQUIRE(closure.order().size() == graph.topoOrder().size());
		REQUIRE(closure.contains(s.source));
		REQUIRE(closure.contains(s.map));
	}
}

TEST_CASE("an interior is stale when its group hands it new values", "[flow][staleness][group]")
{
	Nested scene;
	Evaluation evaluation{scene.graph};
	SerialScheduler{}.run(scene.graph, evaluation);
	const Evaluation& child = evaluation.child(scene.group);
	const NodeId boundary = scene.inner().boundaryInputNode().id();

	SECTION("an edit upstream of the group reseeds it, and everything inside is stale")
	{
		scene.setSource(2);
		const StaleClosure outer(scene.graph, evaluation);
		REQUIRE(outer.contains(scene.group));
		REQUIRE(outer.reseeds(scene.group));

		// Asked with the seed, the interior is stale from its boundary down — though nothing in it
		// changed, and asked WITHOUT it every node in there looks current. That is the whole reason
		// the seed exists: those values were built from the group's old input.
		const StaleClosure seeded(scene.inner(), child, outer.reseeds(scene.group));
		REQUIRE(seeded.contains(boundary));
		REQUIRE(seeded.contains(scene.relay));
		REQUIRE(seeded.contains(scene.offset));
		REQUIRE(StaleClosure(scene.inner(), child).order().empty());
	}
	SECTION("an edit inside the group does not reseed it, and what is upstream of the edit stays current")
	{
		scene.setOffset(100);
		const StaleClosure outer(scene.graph, evaluation);
		REQUIRE(outer.contains(scene.group)); // stale because of what is inside it
		REQUIRE(outer.contains(scene.sink));
		REQUIRE_FALSE(outer.contains(scene.source));
		REQUIRE_FALSE(outer.reseeds(scene.group));

		const StaleClosure inner(scene.inner(), child, outer.reseeds(scene.group));
		REQUIRE_FALSE(inner.contains(boundary));
		REQUIRE_FALSE(inner.contains(scene.relay));
		REQUIRE(inner.contains(scene.offset));

		// And the run agrees: the relay upstream of the edit is not recomputed.
		SerialScheduler{}.run(scene.graph, evaluation);
		REQUIRE(scene.relayCalls == 1);
		REQUIRE(scene.offsetCalls == 2);
	}
}

TEST_CASE("a loop reseeds its interior even for an edit inside its body", "[flow][staleness][loop]")
{
	// A loop re-folds from its seeds whenever it is recomputed at all (ADR-0021): iteration k's inputs
	// are iteration k-1's outputs, so there is no inner incrementality to keep. The same edit inside a
	// GROUP does not reseed it (above) — the rule depends on the interior's kind.
	registerSceneTypes();
	Graph parent;
	std::atomic<int> calls{0};
	const NodeId loopId = parent.add<LoopNode>();
	auto& loop = static_cast<LoopNode&>(parent.node(loopId));
	const LoopNode::Carry carry = loop.addCarry<int>("value");
	Graph& inner = loop.inner();
	const NodeId body = inner.add<Offset>(calls);
	REQUIRE(inner.connect(PortAddress{inner.boundaryInputNode().id(), carry.innerIn}, PortAddress{body, inner.node(body).input(0).id()}) == Connection::Ok);
	REQUIRE(inner.connect(PortAddress{body, inner.node(body).output(0).id()}, PortAddress{inner.boundaryOutputNode().id(), carry.innerOut}) == Connection::Ok);
	edit::syncGroupPorts(parent, loopId);
	const NodeId seed = parent.add(constantOf(1));
	const NodeId bound = parent.add(constantOf(3));
	REQUIRE(parent.connect(PortAddress{seed, parent.node(seed).output(0).id()}, PortAddress{loopId, inputNamed(loop, "value")}) == Connection::Ok);
	REQUIRE(parent.connect(PortAddress{bound, parent.node(bound).output(0).id()}, PortAddress{loopId, loop.countPort()}) == Connection::Ok);

	Evaluation evaluation{parent};
	SerialScheduler{}.run(parent, evaluation);
	REQUIRE(calls == 3);

	auto& offset = static_cast<Offset&>(inner.node(body));
	REQUIRE(offset.setParam(offset.offset, 100));

	const StaleClosure outer(parent, evaluation);
	REQUIRE(outer.contains(loopId));
	REQUIRE_FALSE(outer.contains(seed));
	REQUIRE(outer.reseeds(loopId));
	const StaleClosure seeded(inner, evaluation.child(loopId), outer.reseeds(loopId));
	REQUIRE(seeded.contains(inner.boundaryInputNode().id()));
	REQUIRE(seeded.contains(body));

	// ... and the run agrees: the whole fold again, from its seed.
	SerialScheduler{}.run(parent, evaluation);
	REQUIRE(calls == 6);
}

TEST_CASE("a map with one element owed is stale", "[flow][staleness][map]")
{
	// Asking element 0 alone would call this map clean. It is how a cancelled run leaves one (the
	// cancel suite measures that end to end); here the request is made directly, so the case is about
	// the query.
	Graph graph;
	Calls calls;
	const Scene s = buildScene(graph, calls);
	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(evaluation.childCount(s.map) == 3);
	REQUIRE_FALSE(StaleClosure(graph, evaluation).contains(s.map));

	const Graph& inner = *graph.node(s.map).innerGraph();
	NodeId body;
	for (const NodeId id : inner.nodeIds())
	{
		if (id != inner.boundaryInputNode().id() && id != inner.boundaryOutputNode().id())
			body = id;
	}
	evaluation.child(s.map, 2).requestRecompute(body);

	REQUIRE(StaleClosure(graph, evaluation).contains(s.map));
	REQUIRE_FALSE(StaleClosure(graph, evaluation).contains(s.list));
}

TEST_CASE("a definition of another lineage is all stale, and asking does not throw", "[flow][staleness]")
{
	// prepare() refuses this pairing, because it would act on it; a query only reports, and what it
	// reports is that nothing recorded here describes that definition.
	Graph original;
	Calls originalCalls;
	buildScene(original, originalCalls);
	Evaluation evaluation{original};
	SerialScheduler{}.run(original, evaluation);

	Graph rebuilt;
	Calls rebuiltCalls;
	const Scene s = buildScene(rebuilt, rebuiltCalls);
	REQUIRE(rebuilt.lineage() != original.lineage());

	REQUIRE_NOTHROW(StaleClosure(rebuilt, evaluation));
	const StaleClosure closure(rebuilt, evaluation);
	REQUIRE(closure.order().size() == rebuilt.topoOrder().size());
	REQUIRE(closure.reseeds(s.group));
	REQUIRE(closure.reseeds(s.map));
}
