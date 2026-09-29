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
//   * a throw shows Failed on the node, and on every group it sits inside.
//
// Every run here reads a CLONE and is published, which is the host's shape: the published copy's
// own reads go through the clone, and the question is asked of the document.

#include "freshness.h"

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

using namespace lain;
using flowview::Freshness;
using flowview::GraphPath;
using flowview::levelFreshness;
using flowview::LevelFreshness;
using flowview::PathStep;

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

TEST_CASE("an edit marks the edited node and everything it feeds Stale", "[flowview][freshness]")
{
	Scene scene;
	flow::Evaluation working{scene.document};
	const flow::PublishedEvaluation published = runAndPublish(scene.document, working);

	const LevelFreshness before = levelFreshness(scene.document, published.evaluation(), GraphPath{});
	REQUIRE(before.count(Freshness::Stale) == 0);
	REQUIRE(before.count(Freshness::Failed) == 0);

	static_cast<IntSource&>(scene.document.node(scene.source)).set(2);
	const LevelFreshness after = levelFreshness(scene.document, published.evaluation(), GraphPath{});
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
	const GraphPath inside{PathStep{scene.group, 0}};
	const flow::NodeId boundary = scene.inner().boundaryInputNode().id();

	REQUIRE(levelFreshness(scene.document, published.evaluation(), inside).count(Freshness::Stale) == 0);

	SECTION("an edit upstream of the group: the whole interior, though nothing in it changed")
	{
		static_cast<IntSource&>(scene.document.node(scene.source)).set(2);
		const LevelFreshness level = levelFreshness(scene.document, published.evaluation(), inside);
		REQUIRE(level.of(boundary) == Freshness::Stale);
		REQUIRE(level.of(scene.offset) == Freshness::Stale);
	}
	SECTION("an edit inside the group: the edited node, and not what is upstream of it")
	{
		static_cast<Offset&>(scene.inner().node(scene.offset)).set(100);
		const LevelFreshness level = levelFreshness(scene.document, published.evaluation(), inside);
		REQUIRE(level.of(boundary) == Freshness::Current);
		REQUIRE(level.of(scene.offset) == Freshness::Stale);

		// ... and at the root, the group is Stale because of what is inside it.
		const LevelFreshness root = levelFreshness(scene.document, published.evaluation(), GraphPath{});
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

	const GraphPath element{PathStep{map, 1}};
	REQUIRE(levelFreshness(document, published.evaluation(), element).of(body) == Freshness::Current);

	// An element this publication has no evaluation for — the list grew since — has nothing to show.
	const GraphPath beyond{PathStep{map, 7}};
	REQUIRE(levelFreshness(document, published.evaluation(), beyond).of(body) == Freshness::Stale);

	// A new collection reseeds every element.
	static_cast<MakeInts&>(document.node(list)).set(Ints{4, 5, 6});
	REQUIRE(levelFreshness(document, published.evaluation(), element).of(body) == Freshness::Stale);
}

TEST_CASE("a level nothing has run in is all Stale", "[flowview][freshness]")
{
	// What the panes read between a document swap and its first run: an empty published copy.
	Scene scene;
	const flow::PublishedEvaluation nothing;
	const LevelFreshness root = levelFreshness(scene.document, nothing.evaluation(), GraphPath{});
	REQUIRE(root.count(Freshness::Current) == 0);
	REQUIRE(root.count(Freshness::Stale) == scene.document.nodeIds().size());

	const LevelFreshness inside = levelFreshness(scene.document, nothing.evaluation(), GraphPath{PathStep{scene.group, 0}});
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

	const LevelFreshness root = levelFreshness(document, failed.evaluation(), GraphPath{});
	REQUIRE(root.of(outer) == Freshness::Failed);
	REQUIRE(root.of(source) == Freshness::Current);
	REQUIRE(root.of(sink) == Freshness::Stale); // never reached
	const GraphPath middle{PathStep{outer, 0}};
	REQUIRE(levelFreshness(document, failed.evaluation(), middle).of(innerGroup) == Freshness::Failed);
	const GraphPath bottom{PathStep{outer, 0}, PathStep{innerGroup, 0}};
	REQUIRE(levelFreshness(document, failed.evaluation(), bottom).of(thrower) == Freshness::Failed);

	// Fixed, and run again: Current all the way down.
	*armed = false;
	const flow::PublishedEvaluation fixed = runAndPublish(document, working);
	REQUIRE(levelFreshness(document, fixed.evaluation(), GraphPath{}).count(Freshness::Failed) == 0);
	REQUIRE(levelFreshness(document, fixed.evaluation(), bottom).of(thrower) == Freshness::Current);
}
