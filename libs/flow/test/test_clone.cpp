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
#include "lain/flow/scheduler.h"
#include "testnodes.h"
#include "testscene.h"

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
using namespace lain::flow::test; // the shared scene (testscene.h)

TEST_CASE("a clone is the same recipe at the same point in its history", "[flow][clone]")
{
	Calls calls;
	Graph graph;
	const Scene s = buildScene(graph, calls);
	// An edit before cloning, so the version a clone must carry is one an edit produced rather than
	// the one the node was built with.
	auto& source = static_cast<Source&>(graph.node(s.source));
	const std::uint64_t built = graph.node(s.source).version();
	REQUIRE(source.setParam(source.value, 5));
	REQUIRE(graph.node(s.source).version() != built);

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
	// storage has the same address, so comparing addresses accepted it. Its nodes are new objects, so
	// their versions can no longer coincide with this evaluation's records (a version is drawn from
	// one process-wide sequence) — but an evaluation of one document run against another is a host
	// bug, and lineage refuses it rather than quietly recomputing everything.
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
	// The child-level twin of the case above. The interior is replaced, in the same storage, by one
	// with the same node ids and a different constant. When versions were per-node counters this was
	// a WRONG ANSWER under address identity: the rebuilt nodes could carry the very versions the
	// child had recorded, the child was kept, everything looked clean, and the group served the old
	// value (1, measured). Versions are one process-wide sequence now, so rebuilt nodes can never
	// match — and what is left for the CHILD GUARD to decide is whether the old child is replaced or
	// refused. Lineage replaces it; with the guard reverted to addresses, the child's own prepare
	// refuses the new interior by lineage and the run throws instead.
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

	// The same interior, rebuilt: same boundary ids, same pin, same node id — and, being new node
	// objects, versions no evaluation has seen.
	Graph rebuilt{BoundaryIds{inId, outId}};
	REQUIRE(rebuilt.boundaryOutputNode().addBoundary<int>("y") == outPin);
	REQUIRE(rebuilt.add(std::make_unique<test::ConstInt>(2), constant) == constant);
	REQUIRE(rebuilt.connect(PortAddress{constant, rebuilt.node(constant).output(0).id()},
							PortAddress{outId, outPin}) == Connection::Ok);
	for (const NodeId id : rebuilt.nodeIds())
		REQUIRE(rebuilt.node(id).version() != group.inner().node(id).version());

	const Graph* const address = &group.inner();
	group.inner() = std::move(rebuilt);
	REQUIRE(&group.inner() == address);

	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(evaluation.value(groupOut).get<int>() == 2);
}
