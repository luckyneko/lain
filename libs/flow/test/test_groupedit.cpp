// Tests for edit::groupSelected / edit::ungroup — moving nodes between nesting levels.
//
// The property that matters for both is that the graph still COMPUTES THE SAME THING: grouping is a
// refactor of the document, not of the result. So most cases here run the graph through the real
// SerialScheduler before and after and compare values, rather than only asserting on shape — a
// cut-set that is wired plausibly but wrongly (a pin fed from the wrong side, a fan-out that lost a
// consumer) passes a structural check and fails this one.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/porttyperegistry.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace lain::flow;
using namespace lain::flow::test;

// A payload type nothing registers as a port type — the "unregistered crossing value" case. A
// bespoke type rather than, say, float: a test that depends on some OTHER type happening never to be
// registered breaks silently the day someone registers it.
struct Unnamed
{
};

struct UnnamedSource : Node
{
	PortId out;
	UnnamedSource()
		: Node("UnnamedSource")
	{
		out = addOutput<Unnamed>("thing");
	}
	void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(Unnamed{}); }
};

struct UnnamedSink : Node
{
	PortId in;
	UnnamedSink()
		: Node("UnnamedSink")
	{
		in = addInput<Unnamed>("thing");
	}
	void compute(NodeEvaluation&) const override {}
};

// A crossing value needs its port type registered, or the boundary pin standing for it could not be
// serialized and the gesture refuses. The int-valued fixtures below all need this.
static void registerInt()
{
	registerPortType<int>("Int");
}

// Run `graph` and hand back the evaluation, so a test can read any node's port values. The
// PRODUCTION scheduler, not a stand-in: what is being checked is that the rewired graph still works
// where it actually runs.
static Evaluation run(const Graph& graph)
{
	Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation);
	return evaluation;
}

static int valueAt(const Graph& graph, const Evaluation& evaluation, NodeId node, std::size_t outPort)
{
	return output(graph, evaluation, node, outPort).get<int>();
}

static const Port* portNamed(const Node& node, Port::Direction dir, const std::string& name)
{
	const std::size_t count = (dir == Port::Direction::Input) ? node.inputCount() : node.outputCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		const Port& p = (dir == Port::Direction::Input) ? node.input(i) : node.output(i);
		if (p.name() == name)
			return &p;
	}
	return nullptr;
}

// How many edges leave `node` — for checking that a fan-out kept every consumer.
static int edgesFrom(const Graph& g, NodeId node)
{
	int n = 0;
	for (const Graph::Edge& e : g.edges())
	{
		if (e.from.node == node)
			++n;
	}
	return n;
}

TEST_CASE("groupSelected lowers a node and rewires the graph around it", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId a = g.add<ConstInt>(2);
	const NodeId b = g.add<ConstInt>(3);
	const NodeId sum = g.add<AddInt>();
	const NodeId out = g.add<AddInt>(); // a downstream consumer, so the group has an outgoing edge
	REQUIRE(g.connect(a, 0, sum, 0) == Connection::Ok);
	REQUIRE(g.connect(b, 0, sum, 1) == Connection::Ok);
	REQUIRE(g.connect(sum, 0, out, 0) == Connection::Ok);
	REQUIRE(g.connect(a, 0, out, 1) == Connection::Ok);
	REQUIRE(valueAt(g, run(g), out, 0) == 7); // (2+3) + 2

	const edit::GroupResult result = edit::groupSelected(g, {sum});
	REQUIRE(result.ok());
	REQUIRE(result.moved == std::vector<NodeId>{sum});
	REQUIRE(result.inputPins == 2);	 // one per distinct source feeding the selection
	REQUIRE(result.outputPins == 1); // one per distinct inner source feeding outside

	// The node really moved: gone from the parent, present inside, and carrying the same id.
	REQUIRE_FALSE(g.contains(sum));
	const auto& group = static_cast<const InlineGroupNode&>(g.node(result.group));
	REQUIRE(group.inner().contains(sum));

	// The group's own face mirrors the interface the cut-set implied.
	REQUIRE(g.node(result.group).inputCount() == 2);
	REQUIRE(g.node(result.group).outputCount() == 1);

	// And the graph still computes what it did.
	REQUIRE(valueAt(g, run(g), out, 0) == 7);
}

TEST_CASE("groupSelected gives one boundary pin per distinct source, not per edge", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId src = g.add<ConstInt>(5);
	const NodeId first = g.add<AddInt>();
	const NodeId second = g.add<AddInt>();
	// One source fans out to four inputs across two selected nodes.
	REQUIRE(g.connect(src, 0, first, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, first, 1) == Connection::Ok);
	REQUIRE(g.connect(src, 0, second, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, second, 1) == Connection::Ok);

	const edit::GroupResult result = edit::groupSelected(g, {first, second});
	REQUIRE(result.ok());
	REQUIRE(result.inputPins == 1); // ONE pin, carrying one value — not four copies of it

	// The single pin still reaches all four consumers inside.
	const auto& group = static_cast<const InlineGroupNode&>(g.node(result.group));
	const Graph& inner = group.inner();
	REQUIRE(edgesFrom(inner, inner.boundaryInputNode().id()) == 4);
	REQUIRE(g.node(result.group).inputCount() == 1);

	const Evaluation evaluation = run(g);
	REQUIRE(valueAt(inner, evaluation.child(result.group), first, 0) == 10); // 5 + 5
	REQUIRE(valueAt(inner, evaluation.child(result.group), second, 0) == 10);
}

TEST_CASE("groupSelected keeps every consumer of a value leaving the selection", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId src = g.add<ConstInt>(4);
	const NodeId inside = g.add<AddInt>();
	const NodeId left = g.add<AddInt>();
	const NodeId right = g.add<AddInt>();
	REQUIRE(g.connect(src, 0, inside, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, inside, 1) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, left, 0) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, left, 1) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, right, 0) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, right, 1) == Connection::Ok);

	const edit::GroupResult result = edit::groupSelected(g, {inside});
	REQUIRE(result.ok());
	REQUIRE(result.outputPins == 1);			// one pin ...
	REQUIRE(edgesFrom(g, result.group) == 4);	// ... feeding all four outside inputs
	REQUIRE(valueAt(g, run(g), left, 0) == 16); // (4+4) + (4+4)
	REQUIRE(valueAt(g, run(g), right, 0) == 16);
}

TEST_CASE("groupSelected moves the edges wholly inside the selection with it", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId src = g.add<ConstInt>(1);
	const NodeId first = g.add<AddInt>();
	const NodeId second = g.add<AddInt>();
	REQUIRE(g.connect(src, 0, first, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, first, 1) == Connection::Ok);
	REQUIRE(g.connect(first, 0, second, 0) == Connection::Ok); // internal once both are selected
	REQUIRE(g.connect(src, 0, second, 1) == Connection::Ok);

	const edit::GroupResult result = edit::groupSelected(g, {first, second});
	REQUIRE(result.ok());
	REQUIRE(result.inputPins == 1);
	REQUIRE(result.outputPins == 0); // nothing downstream, so nothing leaves

	// first -> second survived as a plain inner edge, not as a trip through the boundary.
	const auto& group = static_cast<const InlineGroupNode&>(g.node(result.group));
	const Graph& inner = group.inner();
	bool found = false;
	for (const Graph::Edge& e : inner.edges())
		found |= e.from.node == first && e.to.node == second;
	REQUIRE(found);

	const Evaluation evaluation = run(g);
	REQUIRE(valueAt(inner, evaluation.child(result.group), second, 0) == 3); // (1+1) + 1
}

TEST_CASE("groupSelected uniquifies a boundary pin name it would otherwise repeat", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	// Two distinct sources whose output ports carry the SAME name ("value" — ConstInt's).
	const NodeId a = g.add<ConstInt>(1);
	const NodeId b = g.add<ConstInt>(2);
	const NodeId sum = g.add<AddInt>();
	REQUIRE(g.connect(a, 0, sum, 0) == Connection::Ok);
	REQUIRE(g.connect(b, 0, sum, 1) == Connection::Ok);

	const edit::GroupResult result = edit::groupSelected(g, {sum});
	REQUIRE(result.ok());
	REQUIRE(result.inputPins == 2);

	// Both pins exist and are distinct — a duplicate name would make the parent's edges ambiguous on
	// load, since serialization addresses an edge by port NAME.
	const auto& group = static_cast<const InlineGroupNode&>(g.node(result.group));
	const GroupInputNode& boundary = group.inner().boundaryInputNode();
	REQUIRE(portNamed(boundary, Port::Direction::Output, "value") != nullptr);
	REQUIRE(portNamed(boundary, Port::Direction::Output, "value_2") != nullptr);
	REQUIRE(valueAt(g, run(g), a, 0) == 1); // and it still runs
}

TEST_CASE("groupSelected refuses an empty selection", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const std::size_t before = g.nodeCount();
	REQUIRE(edit::groupSelected(g, {}).refusal == edit::GroupRefusal::EmptySelection);
	REQUIRE(edit::groupSelected(g, {NodeId::generate()}).refusal == edit::GroupRefusal::EmptySelection);
	REQUIRE(g.nodeCount() == before); // a refusal changes nothing
}

TEST_CASE("groupSelected refuses a selection containing a boundary node", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId node = g.add<ConstInt>(1);
	const std::size_t before = g.nodeCount();

	// Grouping the graph's own interface would leave the document with no way in or out.
	REQUIRE(edit::groupSelected(g, {node, g.boundaryInputNode().id()}).refusal == edit::GroupRefusal::ContainsBoundary);
	REQUIRE(edit::groupSelected(g, {node, g.boundaryOutputNode().id()}).refusal == edit::GroupRefusal::ContainsBoundary);
	REQUIRE(g.nodeCount() == before);
}

TEST_CASE("groupSelected refuses a selection a value leaves and re-enters", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	// first (selected) -> middle (NOT selected) -> second (selected): contracting the two selected
	// nodes into one would need it to run both before and after `middle`.
	const NodeId src = g.add<ConstInt>(1);
	const NodeId first = g.add<AddInt>();
	const NodeId middle = g.add<AddInt>();
	const NodeId second = g.add<AddInt>();
	REQUIRE(g.connect(src, 0, first, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, first, 1) == Connection::Ok);
	REQUIRE(g.connect(first, 0, middle, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, middle, 1) == Connection::Ok);
	REQUIRE(g.connect(middle, 0, second, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, second, 1) == Connection::Ok);

	const std::size_t before = g.nodeCount();
	const std::size_t edgesBefore = g.edges().size();
	REQUIRE(edit::groupSelected(g, {first, second}).refusal == edit::GroupRefusal::WouldCycle);
	REQUIRE(g.nodeCount() == before); // atomic: no half-built group, no dropped edges
	REQUIRE(g.edges().size() == edgesBefore);

	// Widening the selection to take `middle` in is the way through, and it works.
	REQUIRE(edit::groupSelected(g, {first, middle, second}).ok());
}

TEST_CASE("groupSelected refuses a crossing value whose port type is unregistered", "[flow][group][edit]")
{
	// A boundary pin is replayed on load through its port-type KEY, and the serializer skips a
	// dynamic pin it cannot name — so a group built over an unregistered type would work until saved
	// and come back missing the pin and every edge through it. Refuse instead.
	registerInt(); // Int IS registered; Unnamed never is
	Graph g;
	const NodeId src = g.add<UnnamedSource>();
	const NodeId sink = g.add<UnnamedSink>();
	REQUIRE(g.connect(src, 0, sink, 0) == Connection::Ok);

	const std::size_t before = g.nodeCount();
	REQUIRE(edit::groupSelected(g, {sink}).refusal == edit::GroupRefusal::UnnamedPinType);
	REQUIRE(g.nodeCount() == before);
}

TEST_CASE("ungroup splices an inline group's interior back into its parent", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId a = g.add<ConstInt>(2);
	const NodeId b = g.add<ConstInt>(3);
	const NodeId sum = g.add<AddInt>();
	const NodeId out = g.add<AddInt>();
	REQUIRE(g.connect(a, 0, sum, 0) == Connection::Ok);
	REQUIRE(g.connect(b, 0, sum, 1) == Connection::Ok);
	REQUIRE(g.connect(sum, 0, out, 0) == Connection::Ok);
	REQUIRE(g.connect(a, 0, out, 1) == Connection::Ok);

	const edit::GroupResult grouped = edit::groupSelected(g, {sum});
	REQUIRE(grouped.ok());

	const edit::UngroupResult result = edit::ungroup(g, grouped.group);
	REQUIRE(result.ok());
	REQUIRE(result.moved == std::vector<NodeId>{sum});
	REQUIRE_FALSE(g.contains(grouped.group)); // the group itself is gone
	REQUIRE(g.contains(sum));				  // and its content is back, as itself

	// The two-hop route through the boundary is resolved back to direct edges, and the graph is
	// exactly the one we started with — same values, same node ids.
	REQUIRE(valueAt(g, run(g), out, 0) == 7);
	REQUIRE(result.reconnected == 3); // a -> sum, b -> sum, sum -> out
}

TEST_CASE("ungroup restores a fan-out on both sides of the group", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId src = g.add<ConstInt>(4);
	const NodeId inside = g.add<AddInt>();
	const NodeId left = g.add<AddInt>();
	const NodeId right = g.add<AddInt>();
	REQUIRE(g.connect(src, 0, inside, 0) == Connection::Ok);
	REQUIRE(g.connect(src, 0, inside, 1) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, left, 0) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, left, 1) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, right, 0) == Connection::Ok);
	REQUIRE(g.connect(inside, 0, right, 1) == Connection::Ok);
	const std::size_t edgesBefore = g.edges().size();

	const edit::GroupResult grouped = edit::groupSelected(g, {inside});
	REQUIRE(grouped.ok());
	REQUIRE(edit::ungroup(g, grouped.group).ok());

	// Every one of the six edges is back — a pin standing for a fan-out has to resolve to ALL of its
	// consumers, not just the first one found.
	REQUIRE(g.edges().size() == edgesBefore);
	REQUIRE(valueAt(g, run(g), left, 0) == 16);
	REQUIRE(valueAt(g, run(g), right, 0) == 16);
}

TEST_CASE("ungroup refuses a linked group", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId linked = g.add<LinkedGroupNode>();
	// A linked group's interior is its template's definition, shared with every other instance of it —
	// there is nothing here this document owns to splice out.
	REQUIRE(edit::ungroup(g, linked).refusal == edit::GroupRefusal::NotInline);
	REQUIRE(g.contains(linked));
}

TEST_CASE("ungroup refuses a node that contains no graph", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId plain = g.add<ConstInt>(1);
	REQUIRE(edit::ungroup(g, plain).refusal == edit::GroupRefusal::NotAGroup);
	REQUIRE(edit::ungroup(g, NodeId::generate()).refusal == edit::GroupRefusal::NotAGroup);
	REQUIRE(g.contains(plain));
}

TEST_CASE("replaceGroup swaps what backs a group, keeping the group", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId a = g.add<ConstInt>(2);
	const NodeId b = g.add<ConstInt>(3);
	const NodeId sum = g.add<AddInt>();
	const NodeId out = g.add<AddInt>();
	REQUIRE(g.connect(a, 0, sum, 0) == Connection::Ok);
	REQUIRE(g.connect(b, 0, sum, 1) == Connection::Ok);
	REQUIRE(g.connect(sum, 0, out, 0) == Connection::Ok);
	REQUIRE(g.connect(a, 0, out, 1) == Connection::Ok);

	const edit::GroupResult grouped = edit::groupSelected(g, {sum});
	REQUIRE(grouped.ok());
	g.node(grouped.group).setName("denoise"); // the user's title for it

	// Stand in for what "Make Local" / "Save as Template" hands over: a replacement already carrying
	// an interior with the SAME interface (here, a body built to match pin for pin).
	auto replacement = std::make_unique<InlineGroupNode>();
	{
		Graph body;
		const std::vector<std::string> inNames{
			g.node(grouped.group).input(0).name(), g.node(grouped.group).input(1).name()};
		for (const std::string& name : inNames)
			REQUIRE(addPortOfType(body.boundaryInputNode(), "Int", name) != PortId{});
		REQUIRE(addPortOfType(body.boundaryOutputNode(), "Int",
							  g.node(grouped.group).output(0).name()) != PortId{});
		// Pass the first input straight through, so the whole graph still computes.
		const NodeId pass = body.add<AddInt>();
		REQUIRE(body.connect(PortAddress{body.boundaryInputNode().id(), body.boundaryInputNode().output(0).id()},
							 PortAddress{pass, body.node(pass).input(0).id()}) == Connection::Ok);
		REQUIRE(body.connect(PortAddress{body.boundaryInputNode().id(), body.boundaryInputNode().output(1).id()},
							 PortAddress{pass, body.node(pass).input(1).id()}) == Connection::Ok);
		REQUIRE(body.connect(PortAddress{pass, body.node(pass).output(0).id()},
							 PortAddress{body.boundaryOutputNode().id(), body.boundaryOutputNode().input(0).id()}) == Connection::Ok);
		replacement->inner() = std::move(body);
	}

	const edit::ReplaceResult result = edit::replaceGroup(g, grouped.group, std::move(replacement));
	REQUIRE(result.ok());
	REQUIRE(result.reconnected == 3); // a -> in, b -> in, out <- the group
	REQUIRE(result.dropped == 0);

	// The group is still the same group: same id, same title, same wiring, still computing.
	REQUIRE(g.contains(grouped.group));
	REQUIRE(g.node(grouped.group).name() == "denoise");
	REQUIRE(valueAt(g, run(g), out, 0) == 7); // (2+3) + 2
}

TEST_CASE("replaceGroup reports the edges an interface change could not carry", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId a = g.add<ConstInt>(2);
	const NodeId b = g.add<ConstInt>(3);
	const NodeId sum = g.add<AddInt>();
	REQUIRE(g.connect(a, 0, sum, 0) == Connection::Ok);
	REQUIRE(g.connect(b, 0, sum, 1) == Connection::Ok);

	const edit::GroupResult grouped = edit::groupSelected(g, {sum});
	REQUIRE(grouped.ok());
	REQUIRE(g.node(grouped.group).inputCount() == 2);

	// A replacement whose interface has only ONE of the two pins: the other edge has nowhere to land,
	// and that is a real change to the document rather than something to swallow.
	auto replacement = std::make_unique<InlineGroupNode>();
	REQUIRE(addPortOfType(replacement->inner().boundaryInputNode(), "Int",
						  g.node(grouped.group).input(0).name()) != PortId{});

	const edit::ReplaceResult result = edit::replaceGroup(g, grouped.group, std::move(replacement));
	REQUIRE(result.ok());
	REQUIRE(result.reconnected == 1);
	REQUIRE(result.dropped == 1);
	REQUIRE(g.node(grouped.group).inputCount() == 1);
}

TEST_CASE("replaceGroup refuses a node that is not a group", "[flow][group][edit]")
{
	registerInt();
	Graph g;
	const NodeId plain = g.add<ConstInt>(1);
	REQUIRE(edit::replaceGroup(g, plain, std::make_unique<InlineGroupNode>()).refusal == edit::GroupRefusal::NotAGroup);
	REQUIRE(edit::replaceGroup(g, NodeId::generate(), std::make_unique<InlineGroupNode>()).refusal == edit::GroupRefusal::NotAGroup);
	REQUIRE(g.contains(plain));
}

TEST_CASE("Graph::extract hands the node over, keeping its identity", "[flow][graph]")
{
	Graph source;
	const NodeId id = source.add<ConstInt>(7);
	REQUIRE(source.contains(id));

	std::unique_ptr<Node> node = source.extract(id);
	REQUIRE(node != nullptr);
	REQUIRE_FALSE(source.contains(id));
	REQUIRE(node->id() == id); // a uuid travels with its node — that is what makes the move safe

	Graph target;
	REQUIRE(target.add(std::move(node), id) == id);
	REQUIRE(target.contains(id));

	// Refusals match removeNode's exactly.
	REQUIRE(source.extract(id) == nullptr); // already gone
	REQUIRE(source.extract(source.boundaryInputNode().id()) == nullptr);
	REQUIRE(source.extract(source.boundaryOutputNode().id()) == nullptr);
}
