// Identity across the document boundary (ADR-0011): a node is written under the uuid it actually
// carries, a load restores it AS ITSELF, and the loader recovers deterministically from a document
// whose ids are missing, nil, duplicated or unreadable. Plus the version router: a v1 document
// (numeric ids) is migrated on load and re-saves as v2.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/core/uuid.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using lain::core::Factory;
using lain::core::Uuid;
using lain::data::Value;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	// A constant int source; its value is a param, so it survives a round-trip and can tell two
	// otherwise identical nodes apart.
	class ConstNode : public Node
	{
	public:
		ConstNode()
			: Node("Const")
		{
			addParam<int>("value", 0);
			addOutput<int>("out");
		}
		void compute() override { output(0).set(param(0).get<int>()); }
	};

	class SinkNode : public Node
	{
	public:
		SinkNode()
			: Node("Sink")
		{
			addInput<int>("in");
		}
		void compute() override {}
	};

	Factory<Node> identityFactory()
	{
		lain::flow::registerPortType<int>("Int"); // a boundary pin needs a registry key to serialize
		Factory<Node> factory;
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		factory.registerType<GroupNode>("group");
		factory.registerType<LinkedGroupNode>("linkedGroup");
		factory.registerType<ConstNode>("const");
		factory.registerType<SinkNode>("sink");
		return factory;
	}

	ValueCodecs intCodecs()
	{
		ValueCodecs codecs;
		codecs.registerType<int>("int");
		return codecs;
	}

	// The `nodes` entry whose "kind" is `kind`, or nullptr.
	const Value* nodeOfKind(const Value& document, const std::string& kind)
	{
		const Value* nodes = document.find("nodes");
		if (!nodes || !nodes->asArray())
			return nullptr;
		for (const Value& node : *nodes->asArray())
		{
			const Value* k = node.find("kind");
			if (k && k->asString() && *k->asString() == kind)
				return &node;
		}
		return nullptr;
	}

	const Node* nodeNamed(const Graph& graph, const std::string& name)
	{
		for (const NodeId id : graph.nodeIds())
		{
			if (graph.node(id).name() == name)
				return &graph.node(id);
		}
		return nullptr;
	}
} // namespace

TEST_CASE("a node is written under the uuid it carries", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();
	Graph graph;
	const NodeId source = graph.add<ConstNode>();

	const Value document = toValue(graph, factory, intCodecs());

	const Value* written = nodeOfKind(document, "const");
	REQUIRE(written != nullptr);
	const Value* id = written->find("id");
	REQUIRE(id != nullptr);
	REQUIRE(id->asString() != nullptr);			   // a string, not a number: 128 bits, and readable in a diff
	REQUIRE(*id->asString() == source.toString()); // the node's REAL id — no renumbering on save
	REQUIRE(id->asString()->size() == 36);		   // canonical 8-4-4-4-12
	REQUIRE(Uuid::parse(*id->asString()) == source.uuid());
}

TEST_CASE("a load restores nodes as themselves", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();
	Graph graph;
	const NodeId source = graph.add<ConstNode>();
	const NodeId sink = graph.add<SinkNode>();
	graph.node(source).setParam(graph.node(source).param(0).id(), 11);
	REQUIRE(graph.connect(source, 0, sink, 0) == Connection::Ok);
	const NodeId boundaryIn = graph.boundaryInputNode().id();
	const NodeId boundaryOut = graph.boundaryOutputNode().id();

	const LoadResult result = fromValue(toValue(graph, factory, intCodecs()), factory, intCodecs());
	REQUIRE(result.clean());

	// Every id survives — including the boundary pair's, which the loader seats at construction.
	REQUIRE(result.graph.contains(source));
	REQUIRE(result.graph.contains(sink));
	REQUIRE(result.graph.boundaryInputNode().id() == boundaryIn);
	REQUIRE(result.graph.boundaryOutputNode().id() == boundaryOut);
	REQUIRE(result.graph.node(source).param(0).get<int>() == 11);
	REQUIRE(result.graph.edges().front().from.node == source);
	REQUIRE(result.graph.edges().front().to.node == sink);

	// ... and so does the ORDER, which the nodes array carries rather than a separate field.
	REQUIRE(result.graph.nodeIds() == graph.nodeIds());
}

TEST_CASE("a load preserves identity through nesting", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();
	Graph graph;
	const NodeId groupId = graph.add<GroupNode>();
	auto& group = static_cast<GroupNode&>(graph.node(groupId));
	const NodeId innerConst = group.inner().add<ConstNode>();
	const NodeId innerBoundary = group.inner().boundaryOutputNode().id();

	const LoadResult result = fromValue(toValue(graph, factory, intCodecs()), factory, intCodecs());
	REQUIRE(result.clean());
	REQUIRE(result.graph.contains(groupId));

	// An inline group's body is part of this document, so its nodes come back as themselves too.
	const auto& loaded = static_cast<const GroupNode&>(result.graph.node(groupId));
	REQUIRE(loaded.inner().contains(innerConst));
	REQUIRE(loaded.inner().boundaryOutputNode().id() == innerBoundary);
}

TEST_CASE("the editor section is keyed by node id and lands on the same nodes", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();
	Graph graph;
	const NodeId source = graph.add<ConstNode>();

	EditorData layout;
	Value blob = Value::object();
	blob.set("x", Value(3.0));
	layout[source] = blob;

	const Value document = toValue(graph, factory, intCodecs(), layout);
	const Value* editorSection = document.find("editor");
	REQUIRE(editorSection != nullptr);
	REQUIRE(editorSection->find(source.toString()) != nullptr); // keyed by the id, not by a file index

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE(result.clean());
	REQUIRE(result.editor.nodes.count(source) == 1); // and it comes back on the same node
}

TEST_CASE("a document naming one id twice keeps the first node and re-mints the second", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();
	Graph graph;
	const NodeId first = graph.add<ConstNode>();
	const NodeId second = graph.add<ConstNode>();
	graph.node(first).setParam(graph.node(first).param(0).id(), 1);
	graph.node(second).setParam(graph.node(second).param(0).id(), 2);

	// Forge the collision the way a hand-edited (or badly merged) file would.
	Value document = toValue(graph, factory, intCodecs());
	Value nodes = Value::array();
	for (const Value& node : *document.find("nodes")->asArray())
	{
		Value copy = node;
		const Value* kind = node.find("kind");
		if (kind && kind->asString() && *kind->asString() == "const")
			copy.set("id", Value(first.toString()));
		nodes.push(copy);
	}
	document.set("nodes", nodes);
	document.set("edges", Value::array()); // the forged ids would make the edges ambiguous

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE_FALSE(result.clean()); // the duplicate is REPORTED, not silently absorbed
	REQUIRE(result.graph.nodeCount() == 4);
	REQUIRE(result.graph.contains(first));
	REQUIRE(result.graph.node(first).param(0).get<int>() == 1); // identity is never overwritten
}

TEST_CASE("a document with no usable node id still loads, with fresh identities", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	Value node = Value::object();
	node.set("id", Value("00000000-0000-0000-0000-000000000000")); // nil == "no identity here"
	node.set("kind", Value("const"));
	Value garbled = Value::object();
	garbled.set("id", Value("not-a-uuid")); // a hand-written file with its own naming scheme
	garbled.set("kind", Value("sink"));

	Value nodes = Value::array();
	nodes.push(node);
	nodes.push(garbled);

	// The edge still resolves: the remap is keyed by the id text as written, not by a parsed uuid.
	Value from = Value::object();
	from.set("node", Value("00000000-0000-0000-0000-000000000000"));
	from.set("port", Value("out"));
	Value to = Value::object();
	to.set("node", Value("not-a-uuid"));
	to.set("port", Value("in"));
	Value edge = Value::object();
	edge.set("from", from);
	edge.set("to", to);
	Value edges = Value::array();
	edges.push(edge);

	Value document = Value::object();
	document.set("version", Value(kFormatVersion));
	document.set("nodes", nodes);
	document.set("edges", edges);

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE_FALSE(result.clean()); // both minted identities are reported
	REQUIRE(result.graph.nodeCount() == 4);
	REQUIRE(nodeNamed(result.graph, "Const") != nullptr);
	REQUIRE(nodeNamed(result.graph, "Sink") != nullptr);
	REQUIRE(result.graph.edges().size() == 1);
	REQUIRE(nodeNamed(result.graph, "Const")->id() != NodeId{});
}

TEST_CASE("a document with no boundary node mints one and reports it", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	Value document = Value::object();
	document.set("version", Value(kFormatVersion));
	document.set("nodes", Value::array());
	document.set("edges", Value::array());

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE_FALSE(result.clean()); // the pair is a graph invariant, so its absence is worth saying
	// ... but the file still OPENS: a truncated or hand-written document is not a fatal one.
	REQUIRE(result.graph.nodeCount() == 2);
	REQUIRE(result.graph.boundaryInputNode().id() != NodeId{});
	REQUIRE(result.graph.boundaryOutputNode().id() != result.graph.boundaryInputNode().id());
}

TEST_CASE("a second boundary node is reported and skipped, never merged", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();
	Graph graph;
	graph.boundaryInputNode().addBoundary<int>("a");

	// Duplicate the GroupInput entry, giving the copy its own id and a differently-named pin.
	Value document = toValue(graph, factory, intCodecs());
	const Value* original = nodeOfKind(document, "groupInput");
	REQUIRE(original != nullptr);
	Value duplicate = *original;
	duplicate.set("id", Value(NodeId::generate().toString()));

	Value nodes = *document.find("nodes");
	nodes.push(duplicate);
	document.set("nodes", nodes);

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE_FALSE(result.clean());
	REQUIRE(result.graph.nodeCount() == 2);											  // the pair, and only the pair
	REQUIRE(result.graph.boundaryInputNode().id() == graph.boundaryInputNode().id()); // the FIRST id won
	REQUIRE(result.graph.boundaryInputNode().outputCount() == 1);					  // the duplicate's pins were not merged in
}

TEST_CASE("two linked groups from one template hold distinct node identities", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	// The template: a Const inside, saved as its own document.
	Graph templateGraph;
	const NodeId templateConst = templateGraph.add<ConstNode>();
	const Value templateDocument = toValue(templateGraph, factory, intCodecs());

	// A parent with two links to it.
	Graph parent;
	for (int i = 0; i < 2; ++i)
	{
		const NodeId id = parent.add<LinkedGroupNode>();
		static_cast<LinkedGroupNode&>(parent.node(id)).setSource("shared.json");
	}
	const Value parentDocument = toValue(parent, factory, intCodecs());

	const TemplateResolver resolver = [&](const std::string& source) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{source, templateDocument}; };

	const LoadResult result = fromValue(parentDocument, factory, intCodecs(), resolver);
	REQUIRE(result.clean());

	std::vector<NodeId> instantiated;
	for (const NodeId id : result.graph.nodeIds())
	{
		const Graph* inner = result.graph.node(id).innerGraph();
		if (inner == nullptr)
			continue;
		const Node* inside = nodeNamed(*inner, "Const");
		REQUIRE(inside != nullptr);
		instantiated.push_back(inside->id());
	}
	REQUIRE(instantiated.size() == 2);
	// A template is INSTANTIATED, not restored: loading one file twice must not produce the same
	// id twice, which is the one case where a duplicate would be a certainty rather than a chance.
	REQUIRE(instantiated[0] != instantiated[1]);
	REQUIRE(instantiated[0] != templateConst);
}

// --- the version router + the v1 migration ----------------------------------

namespace
{
	// A version-1 document, in the shape the old writer produced: per-body integer ids, renumbered
	// 1..N, referenced by the edges and the editor section. Written out literally rather than
	// generated, so it keeps testing the format after the v1 writer is long gone.
	Value version1Document()
	{
		const auto endpoint = [](std::int64_t node, const char* port)
		{
			Value ref = Value::object();
			ref.set("node", Value(node));
			ref.set("port", Value(port));
			return ref;
		};
		const auto node = [](std::int64_t id, const char* kind, const char* name)
		{
			Value n = Value::object();
			n.set("id", Value(id));
			n.set("kind", Value(kind));
			n.set("name", Value(name));
			return n;
		};

		Value constNode = node(3, "const", "Const");
		Value param = Value::object();
		param.set("name", Value("value"));
		param.set("type", Value("int"));
		param.set("value", Value(std::int64_t{5}));
		Value params = Value::array();
		params.push(param);
		constNode.set("params", params);

		Value nodes = Value::array();
		nodes.push(node(1, "groupInput", "GroupInput"));
		nodes.push(node(2, "groupOutput", "GroupOutput"));
		nodes.push(constNode);
		nodes.push(node(4, "sink", "Sink"));

		Value edge = Value::object();
		edge.set("from", endpoint(3, "out"));
		edge.set("to", endpoint(4, "in"));
		Value edges = Value::array();
		edges.push(edge);

		Value blob = Value::object();
		blob.set("x", Value(120.0));
		blob.set("y", Value(40.0));
		Value editor = Value::object();
		editor.set("3", blob);

		Value document = Value::object();
		document.set("version", Value(std::int64_t{1}));
		document.set("nodes", nodes);
		document.set("edges", edges);
		document.set("editor", editor);
		return document;
	}
} // namespace

TEST_CASE("a version-1 document migrates on load", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	const LoadResult result = fromValue(version1Document(), factory, intCodecs());
	// Not clean: the migration itself is reported, so a user is told their file is an old format.
	REQUIRE_FALSE(result.clean());
	REQUIRE(result.issues.front().severity == Severity::Warning);

	// Everything a v1 file addressed by number is intact: the nodes, the boundary pair, the param,
	// the edge between two of them, and the editor blob.
	REQUIRE(result.graph.nodeCount() == 4);
	const Node* source = nodeNamed(result.graph, "Const");
	const Node* sink = nodeNamed(result.graph, "Sink");
	REQUIRE(source != nullptr);
	REQUIRE(sink != nullptr);
	REQUIRE(source->param(0).get<int>() == 5);
	REQUIRE(result.graph.edges().size() == 1);
	REQUIRE(result.graph.edges().front().from.node == source->id());
	REQUIRE(result.graph.edges().front().to.node == sink->id());
	REQUIRE(result.editor.nodes.count(source->id()) == 1);
	REQUIRE(result.editor.nodes.at(source->id()).find("x")->asDouble() == 120.0);

	// The ids are MINTED — v1 stored none worth preserving — and they are real uuids.
	REQUIRE(source->id() != NodeId{});
	REQUIRE(Uuid::parse(source->id().toString()) == source->id().uuid());
}

TEST_CASE("a migrated document re-saves as the current version, and then round-trips", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	const LoadResult migrated = fromValue(version1Document(), factory, intCodecs());
	const Value resaved = toValue(migrated.graph, factory, intCodecs(), migrated.editor);
	REQUIRE(resaved.find("version")->asInt64() == kFormatVersion);

	// Once saved, the ids are durable: a second round-trip changes nothing at all.
	const LoadResult reloaded = fromValue(resaved, factory, intCodecs());
	REQUIRE(reloaded.clean()); // no migration warning this time
	REQUIRE(reloaded.graph.nodeIds() == migrated.graph.nodeIds());
	REQUIRE(toValue(reloaded.graph, factory, intCodecs(), reloaded.editor) == resaved);
}

TEST_CASE("an inline group inside a version-1 document migrates too", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	// A v1 inline body numbered its nodes in its OWN namespace, starting from 1 again — so the
	// migration needs a fresh id map per body, or the inner ids would collide with the outer ones.
	Value innerConst = Value::object();
	innerConst.set("id", Value(std::int64_t{3}));
	innerConst.set("kind", Value("const"));
	Value innerNodes = Value::array();
	innerNodes.push(innerConst);
	Value innerBody = Value::object();
	innerBody.set("nodes", innerNodes);
	innerBody.set("edges", Value::array());

	Value groupNode = Value::object();
	groupNode.set("id", Value(std::int64_t{3})); // the SAME number as the node inside it
	groupNode.set("kind", Value("group"));
	groupNode.set("graph", innerBody);

	Value nodes = Value::array();
	nodes.push(groupNode);
	Value document = Value::object();
	document.set("version", Value(std::int64_t{1}));
	document.set("nodes", nodes);
	document.set("edges", Value::array());

	const LoadResult result = fromValue(document, factory, intCodecs());
	const Node* group = nodeNamed(result.graph, "Group");
	REQUIRE(group != nullptr);
	const Graph* inner = group->innerGraph();
	REQUIRE(inner != nullptr);
	const Node* inside = nodeNamed(*inner, "Const");
	REQUIRE(inside != nullptr);
	REQUIRE(inside->id() != group->id()); // two independent numberings, two distinct identities
}

TEST_CASE("a version-1 template linked from a current document is migrated on its own", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	Graph parent;
	const NodeId linkId = parent.add<LinkedGroupNode>();
	static_cast<LinkedGroupNode&>(parent.node(linkId)).setSource("old.json");
	const Value parentDocument = toValue(parent, factory, intCodecs());
	REQUIRE(parentDocument.find("version")->asInt64() == kFormatVersion);

	// A template is a document in its own right — so it enters the version router independently,
	// and an old template linked from a current parent still opens.
	const TemplateResolver resolver = [](const std::string& source) -> std::optional<ResolvedTemplate>
	{ return ResolvedTemplate{source, version1Document()}; };

	const LoadResult result = fromValue(parentDocument, factory, intCodecs(), resolver);
	const auto& linked = static_cast<const LinkedGroupNode&>(result.graph.node(linkId));
	REQUIRE(linked.resolved());
	REQUIRE(nodeNamed(linked.inner(), "Const") != nullptr);
	REQUIRE(nodeNamed(linked.inner(), "Const")->param(0).get<int>() == 5);
}

TEST_CASE("an unknown older version is refused rather than half-read", "[flow-serialize][identity]")
{
	const Factory<Node> factory = identityFactory();

	Value document = Value::object();
	document.set("version", Value(std::int64_t{0}));
	document.set("nodes", Value::array());
	document.set("edges", Value::array());

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE_FALSE(result.clean());
	REQUIRE(result.issues.front().severity == Severity::Error);
	REQUIRE(result.graph.nodeCount() == 2); // an empty graph, still with its invariant pair
}

TEST_CASE("a document with no version is refused rather than guessed at", "[flow-serialize][identity]")
{
	// Guessing "current" would read a v1 file as v2, find no node whose id is a string, drop every
	// one of them — and a save would then write that empty result back over the original.
	const Factory<Node> factory = identityFactory();

	Value node = Value::object();
	node.set("id", Value(std::int64_t{1}));
	node.set("kind", Value("const"));
	Value nodes = Value::array();
	nodes.push(node);

	Value document = Value::object();
	document.set("nodes", nodes);
	document.set("edges", Value::array());

	const LoadResult result = fromValue(document, factory, intCodecs());
	REQUIRE_FALSE(result.clean());
	REQUIRE(result.issues.front().severity == Severity::Error);
	REQUIRE(result.graph.nodeCount() == 2);
	REQUIRE(nodeNamed(result.graph, "Const") == nullptr); // nothing was half-read out of it
}
