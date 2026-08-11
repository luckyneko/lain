// Serializing NESTED graphs (M5 slice 5). An inline group embeds its recipe as a nested BODY; a
// linked group stores a template `source` plus the interface cache. A group's own ports are never
// stored — they are re-derived by edit::syncGroupPorts from the rebuilt inner boundary, which is
// what lets the parent's name-addressed edges land again.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/scheduler.h>
#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using lain::core::Factory;
using lain::data::Value;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	// A constant int source with the value as a PARAM, so it survives a round-trip.
	class ConstNode : public Node
	{
	public:
		ConstNode()
			: Node("Const")
		{
			addParam<int>("value", 0);
			addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(output(0).id()).set(param(0).get<int>());
		}
	};

	// Adds its input to a param.
	class AddNode : public Node
	{
	public:
		AddNode()
			: Node("Add")
		{
			addParam<int>("amount", 0);
			addInput<int>("in");
			addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(output(0).id()).set(evaluation.input(input(0).id()).get<int>() + param(0).get<int>());
		}
	};

	Factory<Node> groupFactory()
	{
		// A boundary pin only serializes if its type has a registry key (portTypeKey), so every test
		// here needs this — and tests run in a random order, so it is done per-fixture, not once.
		lain::flow::registerPortType<int>("Int");

		Factory<Node> factory;
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		factory.registerType<InlineGroupNode>("group");
		factory.registerType<LinkedGroupNode>("linkedGroup");
		factory.registerType<ConstNode>("const");
		factory.registerType<AddNode>("add");
		return factory;
	}

	ValueCodecs intCodecs()
	{
		ValueCodecs codecs;
		codecs.registerType<int>("int");
		return codecs;
	}

	// The inner recipe both the inline and the linked cases use: in -> Add(+amount) -> out.
	void buildAdderInterior(Graph& inner, int amount)
	{
		const NodeId bIn = inner.boundaryInputNode().id();
		const NodeId bOut = inner.boundaryOutputNode().id();
		const PortId inPin = inner.boundaryInputNode().addBoundary<int>("in");
		const PortId outPin = inner.boundaryOutputNode().addBoundary<int>("out");

		const NodeId add = inner.add<AddNode>();
		Node& addNode = inner.node(add);
		addNode.setParam(addNode.param(0).id(), amount);
		REQUIRE(inner.connect({bIn, inPin}, {add, inner.node(add).input(0).id()}) == Connection::Ok);
		REQUIRE(inner.connect({add, inner.node(add).output(0).id()}, {bOut, outPin}) == Connection::Ok);
	}

	// A parent holding one group, fed by a Const and read at the parent's own boundary output.
	// Returns the group's id.
	NodeId buildParent(Graph& g, NodeId group, int seed)
	{
		const NodeId konst = g.add<ConstNode>();
		Node& konstNode = g.node(konst);
		konstNode.setParam(konstNode.param(0).id(), seed);
		const PortId y = g.boundaryOutputNode().addBoundary<int>("y");

		REQUIRE(edit::syncGroupPorts(g, group).added == 2);
		REQUIRE(g.connect({konst, g.node(konst).output(0).id()}, {group, g.node(group).input(0).id()}) == Connection::Ok);
		REQUIRE(g.connect({group, g.node(group).output(0).id()}, {g.boundaryOutputNode().id(), y}) == Connection::Ok);
		return group;
	}

	int runAndRead(const Graph& g) // const: running takes a DEFINITION plus its own Evaluation (M6)
	{
		Evaluation evaluation{g};
		SerialScheduler().run(g, evaluation);
		const std::vector<BoundaryOutput> outputs = g.boundaryOutputs();
		REQUIRE(outputs.size() == 1);
		REQUIRE(evaluation.value(outputs[0]).holds<int>());
		return evaluation.value(outputs[0]).get<int>();
	}
} // namespace

TEST_CASE("an inline group round-trips its whole interior", "[flow-serialize][group]")
{
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph source;
	const NodeId group = source.add<InlineGroupNode>();
	buildAdderInterior(static_cast<InlineGroupNode&>(source.node(group)).inner(), 100);
	buildParent(source, group, 5);
	REQUIRE(runAndRead(source) == 105);

	const Value doc = toValue(source, factory, codecs);

	// The recipe is nested in the node's blob, not flattened into the parent's node list.
	const Value::Array* nodes = doc.find("nodes")->asArray();
	const Value* groupBlob = nullptr;
	for (const Value& n : *nodes)
	{
		if (const Value* kind = n.find("kind"); kind && *kind->asString() == "group")
			groupBlob = &n;
	}
	REQUIRE(groupBlob != nullptr);
	REQUIRE(groupBlob->find("graph") != nullptr);
	REQUIRE(groupBlob->find("graph")->find("nodes") != nullptr);

	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE(result.clean());

	// The loaded group has its interior AND its own re-derived ports, so the parent's edges landed.
	const std::vector<NodeId> ids = result.graph.nodeIds();
	const Node* loadedGroup = nullptr;
	for (const NodeId id : ids)
	{
		if (result.graph.node(id).innerGraph() != nullptr)
			loadedGroup = &result.graph.node(id);
	}
	REQUIRE(loadedGroup != nullptr);
	REQUIRE(loadedGroup->inputCount() == 1); // re-derived, never stored
	REQUIRE(loadedGroup->outputCount() == 1);
	REQUIRE(result.graph.edges().size() == 2);

	// And it still computes the same answer.
	Graph& loaded = const_cast<Graph&>(result.graph);
	REQUIRE(runAndRead(loaded) == 105);

	SECTION("the document is byte-idempotent across a re-save")
	{
		const Value again = toValue(result.graph, factory, codecs);
		REQUIRE(again == doc);
	}
}

TEST_CASE("nesting goes several levels deep through the format", "[flow-serialize][group]")
{
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph source;
	const NodeId outer = source.add<InlineGroupNode>();
	Graph& mid = static_cast<InlineGroupNode&>(source.node(outer)).inner();

	// mid: in -> [inner group +10] -> out
	const NodeId inner = mid.add<InlineGroupNode>();
	buildAdderInterior(static_cast<InlineGroupNode&>(mid.node(inner)).inner(), 10);
	REQUIRE(edit::syncGroupPorts(mid, inner).added == 2);

	const PortId midIn = mid.boundaryInputNode().addBoundary<int>("in");
	const PortId midOut = mid.boundaryOutputNode().addBoundary<int>("out");
	REQUIRE(mid.connect({mid.boundaryInputNode().id(), midIn}, {inner, mid.node(inner).input(0).id()}) == Connection::Ok);
	REQUIRE(mid.connect({inner, mid.node(inner).output(0).id()}, {mid.boundaryOutputNode().id(), midOut}) == Connection::Ok);

	buildParent(source, outer, 1);
	REQUIRE(runAndRead(source) == 11);

	const Value doc = toValue(source, factory, codecs);
	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE(result.clean());

	Graph& loaded = const_cast<Graph&>(result.graph);
	REQUIRE(runAndRead(loaded) == 11);
	REQUIRE(toValue(result.graph, factory, codecs) == doc);
}

TEST_CASE("the editor section nests alongside the graph", "[flow-serialize][group]")
{
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph source;
	const NodeId group = source.add<InlineGroupNode>();
	Graph& inner = static_cast<InlineGroupNode&>(source.node(group)).inner();
	buildAdderInterior(inner, 100);
	buildParent(source, group, 5);

	// Layout for a node at the root AND for one inside the group.
	Value rootBlob = Value::object();
	rootBlob.set("x", Value(1.0));
	Value innerBlob = Value::object();
	innerBlob.set("x", Value(2.0));

	const NodeId innerNode = inner.nodeIds().back(); // the AddNode inside
	EditorTree editor;
	editor.nodes[group] = rootBlob;
	editor.groups[group].nodes[innerNode] = innerBlob;

	const Value doc = toValue(source, factory, codecs, editor);
	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE(result.clean());

	// Root level: re-keyed to the loaded id.
	REQUIRE(result.editor.nodes.size() == 1);
	const NodeId loadedGroup = result.editor.nodes.begin()->first;
	REQUIRE(result.graph.node(loadedGroup).innerGraph() != nullptr);
	REQUIRE(result.editor.nodes.at(loadedGroup).find("x")->asDouble() == 1.0);

	// And the subtree for that group's interior, re-keyed to ITS loaded ids.
	REQUIRE(result.editor.groups.count(loadedGroup) == 1);
	const EditorTree& subtree = result.editor.groups.at(loadedGroup);
	REQUIRE(subtree.nodes.size() == 1);
	REQUIRE(subtree.nodes.begin()->second.find("x")->asDouble() == 2.0);
}

TEST_CASE("a linked group loads its template through the resolver", "[flow-serialize][group]")
{
	lain::flow::registerPortType<int>("Int"); // the interface cache names types by registry key
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	// The template: a standalone document that happens to be linked.
	Graph templateGraph;
	buildAdderInterior(templateGraph, 100);
	const Value templateDoc = toValue(templateGraph, factory, codecs);

	// The parent, holding a linked group pointed at it.
	Graph source;
	const NodeId group = source.add<LinkedGroupNode>();
	auto& linked = static_cast<LinkedGroupNode&>(source.node(group));
	linked.setSource("subs/adder.json");
	// Stand-in for "already resolved" while building: a link's interior is adopted whole, never built
	// through the node — that is ADR-0013's read-only-in-place rule stated by the type.
	Graph interior;
	buildAdderInterior(interior, 100);
	linked.adoptInterior(std::move(interior));
	linked.setResolved(true);
	buildParent(source, group, 5);

	const Value doc = toValue(source, factory, codecs);

	// Saved by reference: the recipe is NOT in the parent, but the interface cache is.
	const Value::Array* nodes = doc.find("nodes")->asArray();
	const Value* blob = nullptr;
	for (const Value& n : *nodes)
	{
		if (const Value* kind = n.find("kind"); kind && *kind->asString() == "linkedGroup")
			blob = &n;
	}
	REQUIRE(blob != nullptr);
	REQUIRE(blob->find("graph") == nullptr); // no embedded recipe
	REQUIRE(*blob->find("source")->asString() == "subs/adder.json");
	REQUIRE(blob->find("interface") != nullptr);

	const TemplateResolver resolver = [&](const std::string& src) -> std::optional<ResolvedTemplate>
	{
		if (src == "subs/adder.json")
			return ResolvedTemplate{"/abs/subs/adder.json", templateDoc};
		return std::nullopt;
	};

	const LoadResult result = fromValue(doc, factory, codecs, resolver);
	REQUIRE(result.clean());

	Graph& loaded = const_cast<Graph&>(result.graph);
	REQUIRE(runAndRead(loaded) == 105); // the template's interior really ran

	SECTION("without a resolver it loads unresolved, but keeps its face and its wiring")
	{
		const LoadResult bare = fromValue(doc, factory, codecs); // no resolver at all
		const Node* node = nullptr;
		for (const NodeId id : bare.graph.nodeIds())
		{
			if (bare.graph.node(id).innerGraph() != nullptr)
				node = &bare.graph.node(id);
		}
		REQUIRE(node != nullptr);
		REQUIRE(node->inputCount() == 1); // pins rebuilt from the cache
		REQUIRE(node->outputCount() == 1);
		REQUIRE(bare.graph.edges().size() == 2); // and the parent's edges still landed

		// Saving an unresolved link is LOSSLESS — the cache is preserved, so it can be repaired.
		const Value resaved = toValue(bare.graph, factory, codecs);
		REQUIRE(resaved == doc);
	}
}

TEST_CASE("a changed template is rectified against the cache and reported", "[flow-serialize][group]")
{
	lain::flow::registerPortType<int>("Int");
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph source;
	const NodeId group = source.add<LinkedGroupNode>();
	auto& linked = static_cast<LinkedGroupNode&>(source.node(group));
	linked.setSource("subs/adder.json");
	Graph interior;
	buildAdderInterior(interior, 100);
	linked.adoptInterior(std::move(interior));
	linked.setResolved(true);
	buildParent(source, group, 5);
	const Value doc = toValue(source, factory, codecs);

	// The template has since LOST its output pin.
	Graph changed;
	const PortId inPin = changed.boundaryInputNode().addBoundary<int>("in");
	(void)inPin;
	const Value changedDoc = toValue(changed, factory, codecs);

	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{"/abs/subs/adder.json", changedDoc}; };

	const LoadResult result = fromValue(doc, factory, codecs, resolver);
	REQUIRE_FALSE(result.clean());

	bool reported = false;
	for (const LoadIssue& issue : result.issues)
	{
		if (issue.message.find("\"out\" no longer exists") != std::string::npos)
			reported = true;
	}
	REQUIRE(reported); // the user is TOLD, rather than left to find missing wiring
}

TEST_CASE("a template that links itself is refused, not followed forever", "[flow-serialize][group]")
{
	lain::flow::registerPortType<int>("Int");
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	// A document containing a linked group that points back at the same file.
	Graph selfish;
	const NodeId group = selfish.add<LinkedGroupNode>();
	static_cast<LinkedGroupNode&>(selfish.node(group)).setSource("loop.json");
	const Value selfishDoc = toValue(selfish, factory, codecs);

	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{"/abs/loop.json", selfishDoc}; };

	const LoadResult result = fromValue(selfishDoc, factory, codecs, resolver);

	bool refused = false;
	for (const LoadIssue& issue : result.issues)
	{
		if (issue.message.find("recursive template") != std::string::npos)
			refused = true;
	}
	REQUIRE(refused);
}

TEST_CASE("a linked group shows its template's own node layout", "[flow-serialize][group]")
{
	// Reported: nodes inside a linked group sat in default columns instead of where the template put
	// them. The template's `editor` section was being discarded on load. It should come with it — the
	// group is read-only, so the template author's arrangement is simply the truthful view, and there
	// is no divergence to manage.
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	// A template with a deliberate, non-default layout.
	Graph templateGraph;
	buildAdderInterior(templateGraph, 100);
	const NodeId inner = templateGraph.nodeIds().back(); // the AddNode
	Value placed = Value::object();
	placed.set("x", Value(275.0));
	placed.set("y", Value(26.0));
	EditorTree templateLayout;
	templateLayout.nodes[inner] = placed;
	const Value templateDoc = toValue(templateGraph, factory, codecs, templateLayout);
	REQUIRE(templateDoc.find("editor") != nullptr);

	// A parent linking it, carrying NO layout of its own for the group's interior.
	Graph source;
	const NodeId group = source.add<LinkedGroupNode>();
	auto& linked = static_cast<LinkedGroupNode&>(source.node(group));
	linked.setSource("subs/adder.json");
	Graph interior;
	buildAdderInterior(interior, 100);
	linked.adoptInterior(std::move(interior));
	linked.setResolved(true);
	const Value doc = toValue(source, factory, codecs);

	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{"/abs/subs/adder.json", templateDoc}; };

	const LoadResult result = fromValue(doc, factory, codecs, resolver);
	REQUIRE(result.clean());

	// The group's subtree carries the template's positions, keyed to the freshly loaded inner ids.
	const NodeId loadedGroup = result.graph.nodeIds().back();
	REQUIRE(result.graph.node(loadedGroup).innerGraph() != nullptr);
	REQUIRE(result.editor.groups.count(loadedGroup) == 1);

	const EditorTree& subtree = result.editor.groups.at(loadedGroup);
	REQUIRE(subtree.nodes.size() == 1);
	REQUIRE(subtree.nodes.begin()->second.find("x")->asDouble() == 275.0);
	REQUIRE(subtree.nodes.begin()->second.find("y")->asDouble() == 26.0);
}

// --- the template cache (M7 / ADR-0013) --------------------------------------
//
// One definition per template, shared by every instance. The point is not memory: it is that a
// template IS one recipe, so an edit to it has one place to land, and two instances differ only in
// their runtime state — which is what ADR-0012 made possible by moving values into an Evaluation.

namespace
{
	// A parent holding `count` linked groups on one template, each fed its own Const and driving its
	// own boundary output, so the instances can be told apart by their results.
	Graph buildMultiInstanceParent(int count, int seedBase)
	{
		Graph parent;
		for (int i = 0; i < count; ++i)
		{
			const NodeId group = parent.add<LinkedGroupNode>();
			auto& linked = static_cast<LinkedGroupNode&>(parent.node(group));
			linked.setSource("adder.json");
			Graph interior; // stand-in for "already resolved" while building the document
			buildAdderInterior(interior, 100);
			linked.adoptInterior(std::move(interior));
			linked.setResolved(true);

			const NodeId konst = parent.add<ConstNode>();
			Node& konstNode = parent.node(konst);
			konstNode.setParam(konstNode.param(0).id(), seedBase + i);
			const PortId y = parent.boundaryOutputNode().addBoundary<int>("y" + std::to_string(i));

			REQUIRE(edit::syncGroupPorts(parent, group).added == 2);
			REQUIRE(parent.connect({konst, parent.node(konst).output(0).id()},
								   {group, parent.node(group).input(0).id()}) == Connection::Ok);
			REQUIRE(parent.connect({group, parent.node(group).output(0).id()},
								   {parent.boundaryOutputNode().id(), y}) == Connection::Ok);
		}
		return parent;
	}

	std::vector<const LinkedGroupNode*> linksOf(const Graph& graph)
	{
		std::vector<const LinkedGroupNode*> links;
		for (const NodeId id : graph.nodeIds())
		{
			if (const auto* linked = dynamic_cast<const LinkedGroupNode*>(&graph.node(id)))
				links.push_back(linked);
		}
		return links;
	}
} // namespace

TEST_CASE("two instances of one template run independently over one shared definition", "[flow-serialize][group]")
{
	// The milestone's whole claim, end to end: two links to one file, one Graph between them, and two
	// results that do not interfere. Before M6 this was impossible — a Port held its value, so the
	// second instance would have overwritten the first's intermediates.
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph templateGraph;
	buildAdderInterior(templateGraph, 100);
	const Value templateDoc = toValue(templateGraph, factory, codecs);

	const Value doc = toValue(buildMultiInstanceParent(2, 5), factory, codecs);

	int reads = 0;
	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{
		++reads;
		return ResolvedTemplate{"/abs/adder.json", templateDoc};
	};

	TemplateCache cache;
	const LoadResult loaded = fromValue(doc, factory, codecs, resolver, &cache);
	REQUIRE(loaded.clean());

	const std::vector<const LinkedGroupNode*> links = linksOf(loaded.graph);
	REQUIRE(links.size() == 2);
	// Pointer identity — two equal copies would satisfy any structural check while still being two
	// definitions, which is the thing being removed.
	REQUIRE(links[0]->definition() == links[1]->definition());
	REQUIRE(cache.size() == 1);
	REQUIRE(reads == 2); // the file is still read per instance; only the BUILD is shared

	Evaluation evaluation{loaded.graph};
	SerialScheduler().run(loaded.graph, evaluation);

	const std::vector<BoundaryOutput> outputs = loaded.graph.boundaryOutputs();
	REQUIRE(outputs.size() == 2);
	REQUIRE(evaluation.value(outputs[0]).get<int>() == 105); // 5 + 100
	REQUIRE(evaluation.value(outputs[1]).get<int>() == 106); // 6 + 100, through the SAME recipe

	// ... and each instance kept its own runtime state: one child Evaluation per group node.
	REQUIRE(evaluation.hasChild(links[0]->id()));
	REQUIRE(evaluation.hasChild(links[1]->id()));
	REQUIRE(&evaluation.child(links[0]->id()) != &evaluation.child(links[1]->id()));

	// The same, in parallel: both instances' inner steps are in ONE flattened plan (ADR-0009) and can
	// therefore run at the same time over the one definition they share. That is the arrangement
	// ADR-0012's "a const Graph& is concurrently readable" exists for, and the first place it is a
	// genuine simultaneous read of a single shared Graph rather than of a private copy.
	lain::task::Executor executor;
	for (int i = 0; i < 50; ++i)
	{
		Evaluation parallelEval{loaded.graph};
		ParallelScheduler{executor}.run(loaded.graph, parallelEval);
		REQUIRE(parallelEval.value(outputs[0]).get<int>() == 105);
		REQUIRE(parallelEval.value(outputs[1]).get<int>() == 106);
	}
}

TEST_CASE("a cached template brings its layout with it", "[flow-serialize][group]")
{
	// A cache entry holds the EditorTree beside the Graph, because loading a template produces both
	// and an instance served from cache would otherwise land its nodes in default columns — which is
	// the bug that made the layout travel in the first place (M5, bug eight).
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph templateGraph;
	buildAdderInterior(templateGraph, 100);
	const NodeId inner = templateGraph.nodeIds().back(); // the AddNode
	Value placed = Value::object();
	placed.set("x", Value(275.0));
	placed.set("y", Value(26.0));
	EditorTree templateLayout;
	templateLayout.nodes[inner] = placed;
	const Value templateDoc = toValue(templateGraph, factory, codecs, templateLayout);

	const Value doc = toValue(buildMultiInstanceParent(2, 1), factory, codecs);
	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{"/abs/adder.json", templateDoc}; };

	TemplateCache cache;
	const LoadResult loaded = fromValue(doc, factory, codecs, resolver, &cache);
	REQUIRE(loaded.clean());

	const std::vector<const LinkedGroupNode*> links = linksOf(loaded.graph);
	REQUIRE(links.size() == 2);
	for (const LinkedGroupNode* link : links) // the second is the one served from cache
	{
		REQUIRE(loaded.editor.groups.count(link->id()) == 1);
		const EditorTree& subtree = loaded.editor.groups.at(link->id());
		REQUIRE(subtree.nodes.size() == 1);
		REQUIRE(subtree.nodes.begin()->second.find("x")->asDouble() == 275.0);
	}
}

TEST_CASE("a template with an unresolved link of its own is not cached", "[flow-serialize][group]")
{
	// A broken link is REPAIRABLE — the missing file may appear a moment later. Caching that state
	// would freeze it until someone reloaded, so the definition is deliberately not stored and the
	// next load heals with no gesture.
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	// The template itself links a file the resolver cannot find.
	Graph broken;
	const NodeId inner = broken.add<LinkedGroupNode>();
	static_cast<LinkedGroupNode&>(broken.node(inner)).setSource("missing.json");
	const Value brokenDoc = toValue(broken, factory, codecs);

	Graph parent;
	const NodeId group = parent.add<LinkedGroupNode>();
	static_cast<LinkedGroupNode&>(parent.node(group)).setSource("broken.json");
	const Value doc = toValue(parent, factory, codecs);

	const TemplateResolver resolver = [&](const std::string& source) -> std::optional<ResolvedTemplate>
	{
		if (source == "broken.json")
			return ResolvedTemplate{"/abs/broken.json", brokenDoc};
		return std::nullopt; // "missing.json" is not there
	};

	TemplateCache cache;
	const LoadResult loaded = fromValue(doc, factory, codecs, resolver, &cache);
	REQUIRE(loaded.graph.nodeCount() == 3); // the boundary pair plus the link — it still loads
	REQUIRE(cache.size() == 0);

	// The outer link IS resolved — it is the definition it resolved to that is incomplete, which is
	// exactly the distinction the structural check makes.
	const std::vector<const LinkedGroupNode*> links = linksOf(loaded.graph);
	REQUIRE(links.size() == 1);
	REQUIRE(links[0]->resolved());
}

TEST_CASE("a self-linking template is refused with a cache in play, and nothing is stored", "[flow-serialize][group]")
{
	// The cache is consulted before a template is BUILT but only written after one is finished, so a
	// partially built definition can never be served — which is what keeps the cycle guard the thing
	// that refuses a loop rather than a race against it.
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph selfish;
	const NodeId group = selfish.add<LinkedGroupNode>();
	static_cast<LinkedGroupNode&>(selfish.node(group)).setSource("loop.json");
	const Value selfishDoc = toValue(selfish, factory, codecs);

	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{"/abs/loop.json", selfishDoc}; };

	TemplateCache cache;
	const LoadResult result = fromValue(selfishDoc, factory, codecs, resolver, &cache);

	bool refused = false;
	for (const LoadIssue& issue : result.issues)
	{
		if (issue.message.find("recursive template") != std::string::npos)
			refused = true;
	}
	REQUIRE(refused);
	REQUIRE(cache.size() == 0); // the inner link is unresolved, so the definition is not whole
}

TEST_CASE("an invalidated template is rebuilt on the next load", "[flow-serialize][group]")
{
	// What the reload gesture and save-invalidates-its-own-path rest on: dropping an entry makes the
	// next load re-read and rebuild, while instances already holding the old definition keep working.
	const Factory<Node> factory = groupFactory();
	const ValueCodecs codecs = intCodecs();

	Graph templateGraph;
	buildAdderInterior(templateGraph, 100);
	Value templateDoc = toValue(templateGraph, factory, codecs);

	const Value doc = toValue(buildMultiInstanceParent(1, 5), factory, codecs);
	const TemplateResolver resolver = [&](const std::string&) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{"/abs/adder.json", templateDoc}; };

	TemplateCache cache;
	const LoadResult first = fromValue(doc, factory, codecs, resolver, &cache);
	REQUIRE(first.clean());
	const std::shared_ptr<const Graph> firstDefinition = linksOf(first.graph)[0]->definition();

	const LoadResult again = fromValue(doc, factory, codecs, resolver, &cache);
	REQUIRE(linksOf(again.graph)[0]->definition() == firstDefinition); // served from the cache

	cache.invalidate("/abs/adder.json");
	const LoadResult afterward = fromValue(doc, factory, codecs, resolver, &cache);
	REQUIRE(afterward.clean());
	REQUIRE(linksOf(afterward.graph)[0]->definition() != firstDefinition); // rebuilt
	REQUIRE(firstDefinition.use_count() >= 1);							   // and the old one is still alive
	REQUIRE(runAndRead(afterward.graph) == 105);
}
