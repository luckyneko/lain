// Freshness (M14 slice 6, ADR-0025): whether each node on the level on screen shows a value that
// reflects the document. Driver-free — levelFreshness is a pure walk over the document and the
// published evaluation (src/freshness.cpp), so it runs here exactly as the frame loop runs it.
//
// What each case pins:
//   * an edit marks the edited node and everything it feeds Stale, and nothing else;
//   * descending into a group whose INPUT changed shows its interior Stale, though nothing in it
//     changed — the walk carries that across the level boundary — while an edit inside the group
//     leaves what is upstream of it Current;
//   * a map element is asked in its own evaluation;
//   * a level nothing has run in is all Stale;
//   * a throw shows Failed on the node, and on every group it sits inside;
//   * what the run in flight reports (M14 slice 7) is laid over it — Queued and Computing on the node,
//     and rolled up onto every group, map and loop they sit inside, by one order: Computing, Failed,
//     Queued, Stale.
//
// Every run here reads a CLONE and is published, which is the host's shape: the published copy's
// own reads go through the clone, and the question is asked of the document.

#include "freshness.h"
#include "runner.h" // RunActivity — what a run in flight reports

#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

using lain::flow::EvalPath;
using lain::flow::EvalStep;
using namespace lain;
using flowview::Freshness;
using flowview::levelFreshness;
using flowview::LevelFreshness;

namespace flowview::test::freshness
{
	using Ints = std::vector<int>;

	// An int source whose value is a param, so a test can edit it the way a user does.
	struct IntSource : flow::Node
	{
		flow::PortId value, out;
		explicit IntSource(int initial)
			: flow::Node("IntSource")
		{
			value = addParam<int>("value", initial);
			out = addOutput<int>("out");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<IntSource>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override { evaluation.output(out).set(param(value).get<int>()); }
		void set(int v) { REQUIRE(setParam(value, v)); }
	};

	// int + a param offset: a body whose recipe can be edited where it sits.
	struct Offset : flow::Node
	{
		flow::PortId offset, in, out;
		Offset()
			: flow::Node("Offset")
		{
			offset = addParam<int>("offset", 10);
			in = addInput<int>("x");
			out = addOutput<int>("y");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<Offset>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(in).get<int>() + param(offset).get<int>());
		}
		void set(int v) { REQUIRE(setParam(offset, v)); }
	};

	// Passes an int through; throws while armed. Armed through a shared flag, so a clone throws too.
	struct Thrower : flow::Node
	{
		std::shared_ptr<bool> armed;
		flow::PortId in, out;
		explicit Thrower(std::shared_ptr<bool> a)
			: flow::Node("Thrower")
			, armed(std::move(a))
		{
			in = addInput<int>("x");
			out = addOutput<int>("y");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<Thrower>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			if (*armed)
				throw std::runtime_error("Thrower: armed");
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// A whole list of ints — what a map maps over.
	struct MakeInts : flow::Node
	{
		flow::PortId values, out;
		explicit MakeInts(Ints initial)
			: flow::Node("MakeInts")
		{
			values = addParam<Ints>("values", std::move(initial));
			out = addOutput<Ints>("items");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<MakeInts>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override { evaluation.output(out).set(param(values).get<Ints>()); }
		void set(Ints v) { REQUIRE(setParam(values, std::move(v))); }
	};

	// Wire `inner`'s boundary through `body` (x -> body -> y) and mirror the group's face.
	static void wireInterior(flow::Graph& parent, flow::NodeId group, flow::Graph& inner, flow::NodeId body)
	{
		const flow::PortId in = inner.boundaryInputNode().addBoundary<int>("x");
		const flow::PortId out = inner.boundaryOutputNode().addBoundary<int>("y");
		REQUIRE(inner.connect(flow::PortAddress{inner.boundaryInputNode().id(), in},
							  flow::PortAddress{body, inner.node(body).input(0).id()}) == flow::Connection::Ok);
		REQUIRE(inner.connect(flow::PortAddress{body, inner.node(body).output(0).id()},
							  flow::PortAddress{inner.boundaryOutputNode().id(), out}) == flow::Connection::Ok);
		flow::edit::syncGroupPorts(parent, group);
	}

	// What the host does: run a CLONE of the document into the working evaluation, and publish it.
	// A run that throws still publishes, as the runner does.
	static flow::PublishedEvaluation runAndPublish(const flow::Graph& document, flow::Evaluation& working)
	{
		auto clone = std::make_shared<const flow::Graph>(document.clone());
		try
		{
			flow::SerialScheduler{}.run(*clone, working);
		}
		catch (const std::runtime_error&)
		{
		}
		return flow::PublishedEvaluation{clone, working};
	}

	// source -> [group: x -> offset -> y] -> sink, beside an unrelated `other`.
	struct Scene
	{
		flow::Graph document;
		flow::NodeId source, group, offset, sink, other;

		Scene()
		{
			source = document.add<IntSource>(1);
			group = document.add<flow::InlineGroupNode>();
			offset = inner().add<Offset>();
			wireInterior(document, group, inner(), offset);
			sink = document.add<Offset>();
			other = document.add<IntSource>(5);
			REQUIRE(document.connect(source, 0, group, 0) == flow::Connection::Ok);
			REQUIRE(document.connect(group, 0, sink, 0) == flow::Connection::Ok);
		}
		Scene(const Scene&) = delete;

		flow::Graph& inner() { return static_cast<flow::InlineGroupNode&>(document.node(group)).inner(); }
	};
} // namespace flowview::test::freshness

using namespace flowview::test::freshness;

// No run in flight: what every case asks about unless it is about a run in flight.
static const flowview::RunActivity idle{};

TEST_CASE("an edit marks the edited node and everything it feeds Stale", "[flowview][freshness]")
{
	Scene scene;
	flow::Evaluation working{scene.document};
	const flow::PublishedEvaluation published = runAndPublish(scene.document, working);

	const LevelFreshness before = levelFreshness(scene.document, published.evaluation(), EvalPath{}, idle);
	REQUIRE(before.count(Freshness::Stale) == 0);
	REQUIRE(before.count(Freshness::Failed) == 0);

	static_cast<IntSource&>(scene.document.node(scene.source)).set(2);
	const LevelFreshness after = levelFreshness(scene.document, published.evaluation(), EvalPath{}, idle);
	REQUIRE(after.of(scene.source) == Freshness::Stale);
	REQUIRE(after.of(scene.group) == Freshness::Stale);
	REQUIRE(after.of(scene.sink) == Freshness::Stale);
	REQUIRE(after.of(scene.other) == Freshness::Current);

	// A node added this frame is not in the map at all, and reads as Stale.
	REQUIRE(after.of(flow::NodeId::generate()) == Freshness::Stale);
}

TEST_CASE("descending into a group shows what its input change made Stale", "[flowview][freshness][group]")
{
	Scene scene;
	flow::Evaluation working{scene.document};
	const flow::PublishedEvaluation published = runAndPublish(scene.document, working);
	const EvalPath inside{EvalStep{scene.group, 0}};
	const flow::NodeId boundary = scene.inner().boundaryInputNode().id();

	REQUIRE(levelFreshness(scene.document, published.evaluation(), inside, idle).count(Freshness::Stale) == 0);

	SECTION("an edit upstream of the group: the whole interior, though nothing in it changed")
	{
		static_cast<IntSource&>(scene.document.node(scene.source)).set(2);
		const LevelFreshness level = levelFreshness(scene.document, published.evaluation(), inside, idle);
		REQUIRE(level.of(boundary) == Freshness::Stale);
		REQUIRE(level.of(scene.offset) == Freshness::Stale);
	}
	SECTION("an edit inside the group: the edited node, and not what is upstream of it")
	{
		static_cast<Offset&>(scene.inner().node(scene.offset)).set(100);
		const LevelFreshness level = levelFreshness(scene.document, published.evaluation(), inside, idle);
		REQUIRE(level.of(boundary) == Freshness::Current);
		REQUIRE(level.of(scene.offset) == Freshness::Stale);

		// ... and at the root, the group is Stale because of what is inside it.
		const LevelFreshness root = levelFreshness(scene.document, published.evaluation(), EvalPath{}, idle);
		REQUIRE(root.of(scene.group) == Freshness::Stale);
		REQUIRE(root.of(scene.source) == Freshness::Current);
	}
}

TEST_CASE("a map element is asked in its own evaluation", "[flowview][freshness][map]")
{
	flow::registerPortType<int>("Int");
	flow::registerPortType<Ints>("ListOfInt");
	flow::Graph document;
	const flow::NodeId list = document.add<MakeInts>(Ints{1, 2, 3});
	const flow::NodeId map = document.add<flow::MapNode>();
	flow::Graph& inner = static_cast<flow::MapNode&>(document.node(map)).inner();
	const flow::NodeId body = inner.add<Offset>();
	wireInterior(document, map, inner, body);
	REQUIRE(document.connect(list, 0, map, 0) == flow::Connection::Ok);

	flow::Evaluation working{document};
	const flow::PublishedEvaluation published = runAndPublish(document, working);
	REQUIRE(published.evaluation().childCount(map) == 3);

	const EvalPath element{EvalStep{map, 1}};
	REQUIRE(levelFreshness(document, published.evaluation(), element, idle).of(body) == Freshness::Current);

	// An element this publication has no evaluation for — the list grew since — has nothing to show.
	const EvalPath beyond{EvalStep{map, 7}};
	REQUIRE(levelFreshness(document, published.evaluation(), beyond, idle).of(body) == Freshness::Stale);

	// A new collection reseeds every element.
	static_cast<MakeInts&>(document.node(list)).set(Ints{4, 5, 6});
	REQUIRE(levelFreshness(document, published.evaluation(), element, idle).of(body) == Freshness::Stale);
}

TEST_CASE("a level nothing has run in is all Stale", "[flowview][freshness]")
{
	// What the panes read between a document swap and its first run: an empty published copy.
	Scene scene;
	const flow::PublishedEvaluation nothing;
	const LevelFreshness root = levelFreshness(scene.document, nothing.evaluation(), EvalPath{}, idle);
	REQUIRE(root.count(Freshness::Current) == 0);
	REQUIRE(root.count(Freshness::Stale) == scene.document.nodeIds().size());

	const LevelFreshness inside = levelFreshness(scene.document, nothing.evaluation(), EvalPath{EvalStep{scene.group, 0}}, idle);
	REQUIRE(inside.count(Freshness::Current) == 0);
	REQUIRE(inside.of(scene.offset) == Freshness::Stale);
}

TEST_CASE("a throw shows Failed on the node and on every group around it", "[flowview][freshness][failure]")
{
	// source -> [outer: x -> [inner: x -> thrower -> y] -> y] -> sink. A group shows the most active
	// state inside it, so the failure is visible from the root without descending — and descending
	// finds it where it happened.
	auto armed = std::make_shared<bool>(true);
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(1);
	const flow::NodeId outer = document.add<flow::InlineGroupNode>();
	flow::Graph& outerInner = static_cast<flow::InlineGroupNode&>(document.node(outer)).inner();
	const flow::NodeId innerGroup = outerInner.add<flow::InlineGroupNode>();
	flow::Graph& deepest = static_cast<flow::InlineGroupNode&>(outerInner.node(innerGroup)).inner();
	const flow::NodeId thrower = deepest.add<Thrower>(armed);
	wireInterior(outerInner, innerGroup, deepest, thrower);
	wireInterior(document, outer, outerInner, innerGroup);
	const flow::NodeId sink = document.add<Offset>();
	REQUIRE(document.connect(source, 0, outer, 0) == flow::Connection::Ok);
	REQUIRE(document.connect(outer, 0, sink, 0) == flow::Connection::Ok);

	flow::Evaluation working{document};
	const flow::PublishedEvaluation failed = runAndPublish(document, working);

	const LevelFreshness root = levelFreshness(document, failed.evaluation(), EvalPath{}, idle);
	REQUIRE(root.of(outer) == Freshness::Failed);
	REQUIRE(root.of(source) == Freshness::Current);
	REQUIRE(root.of(sink) == Freshness::Stale); // never reached
	const EvalPath middle{EvalStep{outer, 0}};
	REQUIRE(levelFreshness(document, failed.evaluation(), middle, idle).of(innerGroup) == Freshness::Failed);
	const EvalPath bottom{EvalStep{outer, 0}, EvalStep{innerGroup, 0}};
	REQUIRE(levelFreshness(document, failed.evaluation(), bottom, idle).of(thrower) == Freshness::Failed);

	// Fixed, and run again: Current all the way down.
	*armed = false;
	const flow::PublishedEvaluation fixed = runAndPublish(document, working);
	REQUIRE(levelFreshness(document, fixed.evaluation(), EvalPath{}, idle).count(Freshness::Failed) == 0);
	REQUIRE(levelFreshness(document, fixed.evaluation(), bottom, idle).of(thrower) == Freshness::Current);
}

//=========================================================================
// The run in flight (M14 slice 7)
//=========================================================================

namespace flowview::test::freshness
{
	// A run's report of what it is doing, built directly: the runner's side is test_runner.cpp's.
	static RunActivity doing(std::initializer_list<std::pair<flowview::NodeAt, flowview::Activity>> entries)
	{
		RunActivity activity;
		for (const auto& entry : entries)
			activity.nodes.emplace(entry.first, entry.second);
		return activity;
	}
} // namespace flowview::test::freshness

using flowview::Activity;
using flowview::NodeAt;
using flowview::RunActivity;

TEST_CASE("the run in flight shows Queued and Computing, and a group shows what is inside it", "[flowview][freshness][group]")
{
	Scene scene;
	flow::Evaluation working{scene.document};
	const flow::PublishedEvaluation published = runAndPublish(scene.document, working);
	static_cast<IntSource&>(scene.document.node(scene.source)).set(2);
	const EvalPath inside{EvalStep{scene.group, 0}};

	SECTION("the source computing, the rest owed")
	{
		const RunActivity activity = doing({{NodeAt{EvalPath{}, scene.source}, Activity::Computing},
											{NodeAt{inside, scene.offset}, Activity::Queued},
											{NodeAt{EvalPath{}, scene.sink}, Activity::Queued}});
		const LevelFreshness root = levelFreshness(scene.document, published.evaluation(), EvalPath{}, activity);
		REQUIRE(root.of(scene.source) == Freshness::Computing);
		REQUIRE(root.of(scene.group) == Freshness::Queued); // from inside it
		REQUIRE(root.of(scene.sink) == Freshness::Queued);
		REQUIRE(root.of(scene.other) == Freshness::Current);

		const LevelFreshness in = levelFreshness(scene.document, published.evaluation(), inside, activity);
		REQUIRE(in.of(scene.offset) == Freshness::Queued);
		// Stale but not owed by this run (nothing reported it): it waits for another.
		REQUIRE(in.of(scene.inner().boundaryInputNode().id()) == Freshness::Stale);
	}
	SECTION("something inside computing: the group is Computing, whatever else waits")
	{
		const RunActivity activity = doing({{NodeAt{inside, scene.offset}, Activity::Computing},
											{NodeAt{EvalPath{}, scene.sink}, Activity::Queued}});
		const LevelFreshness root = levelFreshness(scene.document, published.evaluation(), EvalPath{}, activity);
		REQUIRE(root.of(scene.group) == Freshness::Computing);
		REQUIRE(root.of(scene.source) == Freshness::Stale);
	}
}

TEST_CASE("a map element shows only what the run is doing in that element", "[flowview][freshness][map]")
{
	flow::registerPortType<int>("Int");
	flow::registerPortType<Ints>("ListOfInt");
	flow::Graph document;
	const flow::NodeId list = document.add<MakeInts>(Ints{1, 2, 3});
	const flow::NodeId map = document.add<flow::MapNode>();
	flow::Graph& inner = static_cast<flow::MapNode&>(document.node(map)).inner();
	const flow::NodeId body = inner.add<Offset>();
	wireInterior(document, map, inner, body);
	REQUIRE(document.connect(list, 0, map, 0) == flow::Connection::Ok);
	flow::Evaluation working{document};
	const flow::PublishedEvaluation published = runAndPublish(document, working);

	const RunActivity activity = doing({{NodeAt{EvalPath{EvalStep{map, 2}}, body}, Activity::Computing}});
	REQUIRE(levelFreshness(document, published.evaluation(), EvalPath{}, activity).of(map) == Freshness::Computing);
	REQUIRE(levelFreshness(document, published.evaluation(), EvalPath{EvalStep{map, 2}}, activity).of(body) ==
			Freshness::Computing);
	REQUIRE(levelFreshness(document, published.evaluation(), EvalPath{EvalStep{map, 1}}, activity).of(body) ==
			Freshness::Current);
}

TEST_CASE("Computing outranks Failed, and Failed outranks Queued", "[flowview][freshness][failure]")
{
	// source -> [outer: x -> thrower -> y]. The thrower has failed; a run in flight owes it again.
	auto armed = std::make_shared<bool>(true);
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(1);
	const flow::NodeId outer = document.add<flow::InlineGroupNode>();
	flow::Graph& inner = static_cast<flow::InlineGroupNode&>(document.node(outer)).inner();
	const flow::NodeId thrower = inner.add<Thrower>(armed);
	const flow::NodeId relay = inner.add<Offset>();
	wireInterior(document, outer, inner, thrower);
	REQUIRE(document.connect(source, 0, outer, 0) == flow::Connection::Ok);
	flow::Evaluation working{document};
	const flow::PublishedEvaluation failed = runAndPublish(document, working);
	const EvalPath inside{EvalStep{outer, 0}};

	SECTION("owed again, not yet started: still Failed — its last compute threw, and a failure inside a group shows")
	{
		const RunActivity activity = doing({{NodeAt{inside, thrower}, Activity::Queued},
											{NodeAt{inside, relay}, Activity::Queued}});
		REQUIRE(levelFreshness(document, failed.evaluation(), inside, activity).of(thrower) == Freshness::Failed);
		REQUIRE(levelFreshness(document, failed.evaluation(), EvalPath{}, activity).of(outer) == Freshness::Failed);
	}
	SECTION("its retry running: Computing, on it and on the group around it")
	{
		const RunActivity activity = doing({{NodeAt{inside, thrower}, Activity::Computing}});
		REQUIRE(levelFreshness(document, failed.evaluation(), inside, activity).of(thrower) == Freshness::Computing);
		REQUIRE(levelFreshness(document, failed.evaluation(), EvalPath{}, activity).of(outer) == Freshness::Computing);
	}
	SECTION("Queued over Stale: a node the run owes will update by itself")
	{
		const RunActivity activity = doing({{NodeAt{EvalPath{}, source}, Activity::Queued}});
		static_cast<IntSource&>(document.node(source)).set(3);
		REQUIRE(levelFreshness(document, failed.evaluation(), EvalPath{}, activity).of(source) == Freshness::Queued);
		REQUIRE(levelFreshness(document, failed.evaluation(), EvalPath{}, idle).of(source) == Freshness::Stale);
	}
}
