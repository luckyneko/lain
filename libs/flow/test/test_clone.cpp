// Clone + lineage (M14 slice 1, ADR-0025). A run is going to read a CLONE of the document, so an
// edit never races it — which only works if one Evaluation stays incremental across clones. A clone
// therefore preserves identity AND history (NodeIds, per-node versions, lineage), and an Evaluation
// pairs with a definition by LINEAGE rather than by address.
//
// Checked on values and call counts through the production schedulers, never on structure alone:
// a clone that looks right and recomputes everything would pass every structural assertion here.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/porttyperegistry.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

using namespace lain::flow;

namespace
{
	using Ints = std::vector<int>;

	void registerCloneTypes()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		registerPortType<int>("Int");
		registerPortType<Ints>("ListOfInt");
	}

	// An int source whose value is a PARAM, so editing it is a recipe change (a version bump) — the
	// edit the incremental case makes between clones. The counter is held by REFERENCE: a clone
	// copies the node, so a clone counts into the same place, which is what lets a test add up the
	// work done across several clones.
	struct Source : Node
	{
		std::atomic<int>& calls;
		PortId value, out;
		Source(std::atomic<int>& c, int initial)
			: Node("Source")
			, calls(c)
		{
			value = addParam<int>("value", initial);
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Source>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(param(value).get<int>());
		}
	};

	// Passes an int through, counted.
	struct Relay : Node
	{
		std::atomic<int>& calls;
		PortId in, out;
		explicit Relay(std::atomic<int>& c)
			: Node("Relay")
			, calls(c)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Relay>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// A whole collection, counted — what the map maps over.
	struct MakeInts : Node
	{
		std::atomic<int>& calls;
		Ints values;
		PortId out;
		MakeInts(std::atomic<int>& c, Ints v)
			: Node("MakeInts")
			, calls(c)
			, values(std::move(v))
		{
			out = addOutput<Ints>("items");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<MakeInts>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(values);
		}
	};

	// Where each piece of work is counted.
	struct Calls
	{
		std::atomic<int> source{0};	 // the root source feeding the group
		std::atomic<int> inner{0};	 // the relay INSIDE the inline group
		std::atomic<int> sink{0};	 // the root relay downstream of the group
		std::atomic<int> list{0};	 // the collection the map maps over
		std::atomic<int> element{0}; // the relay inside the map, once per element
	};

	// source -> [group: relay] -> sink, and beside it list -> [map: relay]. Two independent chains, so
	// an edit to the first can be shown to leave the second alone — and both kinds of child
	// evaluation (a group's one, a map's N) have to survive being prepared against a clone.
	struct Scene
	{
		NodeId source;
		NodeId group;
		NodeId sink;
		NodeId map;
	};

	// Wire an interior `in -> body -> out` between its own boundary pins, then mirror its face.
	void buildInterior(Graph& parent, NodeId groupId, Graph& inner, std::unique_ptr<Node> body)
	{
		const PortId inPin = inner.boundaryInputNode().addBoundary<int>("x");
		const PortId outPin = inner.boundaryOutputNode().addBoundary<int>("y");
		const NodeId bodyId = inner.add(std::move(body));
		REQUIRE(inner.connect(PortAddress{inner.boundaryInputNode().id(), inPin},
							  PortAddress{bodyId, inner.node(bodyId).input(0).id()}) == Connection::Ok);
		REQUIRE(inner.connect(PortAddress{bodyId, inner.node(bodyId).output(0).id()},
							  PortAddress{inner.boundaryOutputNode().id(), outPin}) == Connection::Ok);
		edit::syncGroupPorts(parent, groupId);
	}

	Scene buildScene(Graph& graph, Calls& calls)
	{
		registerCloneTypes();
		Scene s{};
		s.source = graph.add<Source>(calls.source, 1);

		s.group = graph.add<InlineGroupNode>();
		auto& group = static_cast<InlineGroupNode&>(graph.node(s.group));
		buildInterior(graph, s.group, group.inner(), std::make_unique<Relay>(calls.inner));

		s.sink = graph.add<Relay>(calls.sink);
		REQUIRE(graph.connect(s.source, 0, s.group, 0) == Connection::Ok);
		REQUIRE(graph.connect(s.group, 0, s.sink, 0) == Connection::Ok);

		const NodeId list = graph.add<MakeInts>(calls.list, Ints{1, 2, 3});
		s.map = graph.add<MapNode>();
		auto& map = static_cast<MapNode&>(graph.node(s.map));
		buildInterior(graph, s.map, map.inner(), std::make_unique<Relay>(calls.element));
		REQUIRE(graph.connect(list, 0, s.map, 0) == Connection::Ok);
		return s;
	}

	int intOut(const Graph& graph, const Evaluation& evaluation, NodeId node)
	{
		return test::output(graph, evaluation, node, 0).get<int>();
	}
} // namespace

TEST_CASE("a clone is the same recipe at the same point in its history", "[flow][clone]")
{
	Calls calls;
	Graph graph;
	const Scene s = buildScene(graph, calls);
	// An edit before cloning, so the versions being compared are not all the initial 1.
	auto& source = static_cast<Source&>(graph.node(s.source));
	REQUIRE(source.setParam(source.value, 5));
	REQUIRE(graph.node(s.source).version() > 1);

	const Graph copy = graph.clone();

	REQUIRE(&copy != &graph);
	REQUIRE(copy.lineage() == graph.lineage());
	REQUIRE(copy.nodeIds() == graph.nodeIds()); // identity AND insertion order
	REQUIRE(copy.topoOrder() == graph.topoOrder());
	REQUIRE(copy.boundaryInputNode().id() == graph.boundaryInputNode().id());
	REQUIRE(copy.boundaryOutputNode().id() == graph.boundaryOutputNode().id());
	REQUIRE(copy.edges().size() == graph.edges().size());
	for (std::size_t i = 0; i < graph.edges().size(); ++i)
	{
		REQUIRE(copy.edges()[i].from == graph.edges()[i].from);
		REQUIRE(copy.edges()[i].to == graph.edges()[i].to);
	}
	for (const NodeId id : graph.nodeIds())
	{
		const Node& original = graph.node(id);
		const Node& cloned = copy.node(id);
		INFO("node: " << original.name());
		REQUIRE(&cloned != &original);
		REQUIRE(typeid(cloned) == typeid(original)); // not sliced to a base
		REQUIRE(cloned.version() == original.version());
		REQUIRE(cloned.name() == original.name());
		REQUIRE(cloned.inputCount() == original.inputCount());
		REQUIRE(cloned.outputCount() == original.outputCount());
	}
	REQUIRE(static_cast<const Source&>(copy.node(s.source)).param(source.value).get<int>() == 5);

	// An interior is part of the recipe, so it clones with the same history too.
	const Graph& inner = *graph.node(s.group).innerGraph();
	const Graph& clonedInner = *copy.node(s.group).innerGraph();
	REQUIRE(clonedInner.lineage() == inner.lineage());
	REQUIRE(clonedInner.nodeIds() == inner.nodeIds());
}

TEST_CASE("a clone does not see edits made to the document afterwards", "[flow][clone]")
{
	Calls calls;
	Graph graph;
	const Scene s = buildScene(graph, calls);
	const Graph copy = graph.clone();

	// Owned interiors are the clone's own objects — a deep copy, not a second name for one graph.
	REQUIRE(copy.node(s.group).innerGraph() != graph.node(s.group).innerGraph());
	REQUIRE(copy.node(s.map).innerGraph() != graph.node(s.map).innerGraph());

	// Now edit the document every way a pane can.
	auto& source = static_cast<Source&>(graph.node(s.source));
	const std::uint64_t cloneVersion = copy.node(s.source).version();
	REQUIRE(source.setParam(source.value, 9));										  // a param
	REQUIRE(graph.disconnect(PortAddress{s.sink, graph.node(s.sink).input(0).id()})); // an edge
	auto& group = static_cast<InlineGroupNode&>(graph.node(s.group));
	group.inner().add<test::ConstInt>(4);				// a node inside an interior
	graph.boundaryInputNode().addBoundary<int>("late"); // a dynamic pin

	REQUIRE(static_cast<const Source&>(copy.node(s.source)).param(source.value).get<int>() == 1);
	REQUIRE(copy.node(s.source).version() == cloneVersion);
	REQUIRE(copy.edges().size() == graph.edges().size() + 1);
	REQUIRE(copy.node(s.group).innerGraph()->nodeCount() == group.inner().nodeCount() - 1);
	REQUIRE(copy.boundaryInputNode().outputCount() == graph.boundaryInputNode().outputCount() - 1);
}

TEST_CASE("a clone shares a linked group's definition rather than copying it", "[flow][clone]")
{
	// A template definition is immutable and already shared between instances (ADR-0013), so a clone
	// is simply one more sharer — copying it would cost a whole graph per run and buy nothing.
	Graph graph;
	const NodeId linked = graph.add<LinkedGroupNode>();
	auto& node = static_cast<LinkedGroupNode&>(graph.node(linked));
	node.adoptInterior(std::make_shared<const Graph>());
	node.setSource("template.json");

	const Graph copy = graph.clone();
	const auto& cloned = static_cast<const LinkedGroupNode&>(copy.node(linked));
	REQUIRE(cloned.definition() == node.definition());
	REQUIRE(cloned.source() == "template.json");
}

TEST_CASE("one evaluation stays incremental across clones of its document", "[flow][clone][evaluation]")
{
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	Evaluation evaluation{document};

	// The runner's shape (M14 slice 4): each run reads a FRESH clone, which is destroyed before the
	// next — so nothing about the evaluation may depend on meeting the same Graph object twice.
	auto runOnAClone = [&](auto&& scheduler)
	{
		const Graph clone = document.clone();
		scheduler.run(clone, evaluation);
		return intOut(clone, evaluation, s.sink);
	};

	auto exercise = [&](auto&& makeScheduler)
	{
		REQUIRE(runOnAClone(makeScheduler()) == 1);
		REQUIRE(calls.source == 1);
		REQUIRE(calls.inner == 1);
		REQUIRE(calls.sink == 1);
		REQUIRE(calls.list == 1);
		REQUIRE(calls.element == 3); // once per element

		// Nothing changed: a new clone is a new object with the same history, so nothing is stale.
		// Asked by address, the ROOT check refuses this clone outright; the CHILD check alone
		// rebuilds every child evaluation, discarding what both interiors held — measured: the
		// group's result goes EMPTY, since a clean group does not republish into a fresh child.
		runOnAClone(makeScheduler());
		REQUIRE(calls.source == 1);
		REQUIRE(calls.inner == 1);
		REQUIRE(calls.sink == 1);
		REQUIRE(calls.list == 1);
		REQUIRE(calls.element == 3);

		// Edit the DOCUMENT — never a clone — and run a clone of it. Only the edited node and what is
		// downstream of it recompute; the map's chain, which the edit does not reach, does not.
		auto& source = static_cast<Source&>(document.node(s.source));
		REQUIRE(source.setParam(source.value, 7));
		REQUIRE(runOnAClone(makeScheduler()) == 7);
		REQUIRE(calls.source == 2);
		REQUIRE(calls.inner == 2);
		REQUIRE(calls.sink == 2);
		REQUIRE(calls.list == 1);
		REQUIRE(calls.element == 3);
	};

	SECTION("serial")
	{
		exercise([]
				 { return SerialScheduler{}; });
	}

	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		exercise([]
				 { return ParallelScheduler{}; });
	}
}

TEST_CASE("an evaluation refuses a graph rebuilt where the old one stood", "[flow][clone][evaluation]")
{
	// The hole address identity could not close (ADR-0012 conceded it): a Graph rebuilt in the SAME
	// storage has the same address, so comparing addresses accepted it — and its versions restart,
	// so comparing them against this evaluation's records would call changed nodes clean.
	Graph graph;
	const NodeId source = graph.add<test::ConstInt>(1);
	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	// Rebuilt with the very same ids, then moved into the very same object.
	Graph rebuilt{BoundaryIds{graph.boundaryInputNode().id(), graph.boundaryOutputNode().id()}};
	REQUIRE(rebuilt.add(std::make_unique<test::ConstInt>(2), source) == source);
	const Graph* const address = &graph;
	graph = std::move(rebuilt);
	REQUIRE(&graph == address);
	REQUIRE(graph.contains(source));

	REQUIRE_THROWS_AS(SerialScheduler{}.run(graph, evaluation), std::logic_error);

	// ... while a clone of it, a different object altogether, is accepted by an evaluation of ITS
	// lineage — the other half of the same rule.
	Evaluation fresh{graph};
	const Graph clone = graph.clone();
	REQUIRE_NOTHROW(SerialScheduler{}.run(clone, fresh));
	REQUIRE(intOut(clone, fresh, source) == 2);
}

TEST_CASE("an interior rebuilt where the old one stood gets a fresh child evaluation", "[flow][clone][evaluation]")
{
	// The child-level twin of the case above, and the one where address identity gives a WRONG
	// ANSWER rather than a missed refusal. The interior is replaced by one with the same node ids
	// and the same versions but a different constant, in the same storage: comparing addresses kept
	// the child, every inner node looked clean, and the group went on serving the old value (1,
	// measured against the pre-change checks). Lineage at the CHILD GUARD is what turns that into a
	// fresh child rather than an error: with the guard alone reverted, the child's own prepare still
	// refuses the new interior by lineage, and the run throws instead of rebuilding.
	Graph graph;
	const NodeId groupId = graph.add<InlineGroupNode>();
	auto& group = static_cast<InlineGroupNode&>(graph.node(groupId));

	const NodeId inId = group.inner().boundaryInputNode().id();
	const NodeId outId = group.inner().boundaryOutputNode().id();
	const PortId outPin = group.inner().boundaryOutputNode().addBoundary<int>("y");
	const NodeId constant = group.inner().add<test::ConstInt>(1);
	REQUIRE(group.inner().connect(PortAddress{constant, group.inner().node(constant).output(0).id()},
								  PortAddress{outId, outPin}) == Connection::Ok);
	edit::syncGroupPorts(graph, groupId);
	const PortAddress groupOut{groupId, graph.node(groupId).output(0).id()};

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(evaluation.value(groupOut).get<int>() == 1);

	// The same interior, rebuilt: same boundary ids, same pin, same node id, same versions.
	Graph rebuilt{BoundaryIds{inId, outId}};
	REQUIRE(rebuilt.boundaryOutputNode().addBoundary<int>("y") == outPin);
	REQUIRE(rebuilt.add(std::make_unique<test::ConstInt>(2), constant) == constant);
	REQUIRE(rebuilt.connect(PortAddress{constant, rebuilt.node(constant).output(0).id()},
							PortAddress{outId, outPin}) == Connection::Ok);
	for (const NodeId id : rebuilt.nodeIds())
		REQUIRE(rebuilt.node(id).version() == group.inner().node(id).version());

	const Graph* const address = &group.inner();
	group.inner() = std::move(rebuilt);
	REQUIRE(&group.inner() == address);

	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(evaluation.value(groupOut).get<int>() == 2);
}
