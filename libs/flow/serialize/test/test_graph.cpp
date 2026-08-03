// The graph walk end to end: a Graph of GPU-free nodes round-trips through data::Value (structure +
// params + name-addressed edges), survives the full Graph -> Value -> JSON -> Value -> Graph
// pipeline, and reports issues (too-new version, unknown kind) best-effort.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode — the factory registers the pair
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/io/data/json/register.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

using lain::core::Factory;
using lain::data::Value;
using lain::flow::Connection;
using lain::flow::Graph;
using lain::flow::Node;
using lain::flow::NodeId;
using lain::flow::serialize::fromValue;
using lain::flow::serialize::LoadResult;
using lain::flow::serialize::Severity;
using lain::flow::serialize::toValue;
using lain::flow::serialize::ValueCodecs;

// Every Graph is born with its boundary pair (one GroupInput + one GroupOutput), so nodeCount()
// is that many more than the nodes a test added itself.
static constexpr std::size_t kBoundaryNodes = 2;

class SourceNode : public Node
{
public:
	SourceNode()
		: Node("Source")
	{
		addOutput<int>("out");
		addParam<int>("seed", 7);
	}
	void compute() override { output(0).set<int>(param(0).get<int>()); }
};

class SinkNode : public Node
{
public:
	SinkNode()
		: Node("Sink")
	{
		addInput<int>("in");
		addParam<float>("scale", 1.5f);
	}
	void compute() override {}
};

static Factory<Node> nodeFactory()
{
	Factory<Node> factory;
	// The boundary pair is registered because every Graph HAS one: a factory that cannot name it
	// writes a document with no interface, which the loader then reports. Matching production here
	// also means these round-trips carry the pair's identity, not just the added nodes'.
	factory.registerType<lain::flow::GroupInputNode>("groupInput");
	factory.registerType<lain::flow::GroupOutputNode>("groupOutput");
	factory.registerType<SourceNode>("source");
	factory.registerType<SinkNode>("sink");
	return factory;
}

static ValueCodecs valueCodecs()
{
	ValueCodecs codecs;
	codecs.registerType<int>("int");
	codecs.registerType<float>("float");
	return codecs;
}

// Source(seed=42) -> Sink(scale=9.0), connected out -> in.
static Graph sampleGraph()
{
	Graph graph;
	const NodeId source = graph.add<SourceNode>();
	const NodeId sink = graph.add<SinkNode>();
	Node& sourceNode = graph.node(source);
	Node& sinkNode = graph.node(sink);
	sourceNode.setParam(sourceNode.param(0).id(), 42);
	sinkNode.setParam(sinkNode.param(0).id(), 9.0f);
	REQUIRE(graph.connect(source, 0, sink, 0) == Connection::Ok);
	return graph;
}

static const Node* nodeNamed(const Graph& graph, const std::string& name)
{
	for (const NodeId id : graph.nodeIds())
	{
		if (graph.node(id).name() == name)
			return &graph.node(id);
	}
	return nullptr;
}

TEST_CASE("a graph round-trips through data::Value (structure + params + edges)", "[flow-serialize]")
{
	const Factory<Node> factory = nodeFactory();
	const ValueCodecs codecs = valueCodecs();
	const Graph graph = sampleGraph();

	const Value doc = toValue(graph, factory, codecs);
	REQUIRE(doc.find("version") != nullptr);

	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE(result.clean());
	REQUIRE(result.graph.nodeCount() == kBoundaryNodes + 2);
	REQUIRE(result.graph.edges().size() == 1);

	const Node* source = nodeNamed(result.graph, "Source");
	const Node* sink = nodeNamed(result.graph, "Sink");
	REQUIRE(source != nullptr);
	REQUIRE(sink != nullptr);
	REQUIRE(source->param(0).get<int>() == 42); // params restored
	REQUIRE(sink->param(0).get<float>() == 9.0f);
}

TEST_CASE("a user-renamed node keeps its title across a round-trip", "[flow-serialize]")
{
	const Factory<Node> factory = nodeFactory();
	const ValueCodecs codecs = valueCodecs();
	Graph graph = sampleGraph();
	// A node's title is user-editable (Node::setName) — the factory kind, not the name, is what
	// rebuilds it on load, so the renamed node must come back renamed rather than as "Source".
	for (const NodeId id : graph.nodeIds())
	{
		if (graph.node(id).name() == "Source")
			graph.node(id).setName("warm source");
	}

	const LoadResult result = fromValue(toValue(graph, factory, codecs), factory, codecs);
	REQUIRE(result.clean());
	REQUIRE(nodeNamed(result.graph, "warm source") != nullptr);
	REQUIRE(nodeNamed(result.graph, "Source") == nullptr);
	REQUIRE(nodeNamed(result.graph, "warm source")->param(0).get<int>() == 42); // still the same node
	REQUIRE(nodeNamed(result.graph, "Sink") != nullptr);						// an unrenamed node keeps its ctor name
}

TEST_CASE("a graph survives Graph -> Value -> JSON -> Value -> Graph", "[flow-serialize]")
{
	lain::io::data::json::registerCodec();
	const Factory<Node> factory = nodeFactory();
	const ValueCodecs codecs = valueCodecs();
	const Graph graph = sampleGraph();

	const Value doc = toValue(graph, factory, codecs);
	const auto bytes = lain::io::data::encode("json", doc);
	REQUIRE(bytes.has_value());
	const auto back = lain::io::data::decode("json", *bytes);
	REQUIRE(back.has_value());

	const LoadResult result = fromValue(*back, factory, codecs);
	REQUIRE(result.clean()); // Int/UInt tolerance: node ids + the seed survive the JSON collapse
	REQUIRE(result.graph.nodeCount() == kBoundaryNodes + 2);
	REQUIRE(result.graph.edges().size() == 1);
	REQUIRE(nodeNamed(result.graph, "Source")->param(0).get<int>() == 42);
}

TEST_CASE("a too-new document version is a fatal load", "[flow-serialize]")
{
	const Factory<Node> factory = nodeFactory();
	const ValueCodecs codecs = valueCodecs();

	Value doc = Value::object();
	doc.set("version", Value(std::int64_t{999}));
	doc.set("nodes", Value::array());
	doc.set("edges", Value::array());

	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE_FALSE(result.clean());
	REQUIRE(result.graph.nodeCount() == kBoundaryNodes + 0);
	REQUIRE(result.issues.front().severity == Severity::Error);
}

TEST_CASE("an unknown node kind is skipped with an Error issue", "[flow-serialize]")
{
	const Factory<Node> factory = nodeFactory();
	const ValueCodecs codecs = valueCodecs();

	Value node = Value::object();
	node.set("id", Value(std::int64_t{1}));
	node.set("kind", Value("nonesuch"));
	Value nodes = Value::array();
	nodes.push(node);

	Value doc = Value::object();
	doc.set("version", Value(std::int64_t{1}));
	doc.set("nodes", nodes);
	doc.set("edges", Value::array());

	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE_FALSE(result.clean());
	REQUIRE(result.graph.nodeCount() == kBoundaryNodes + 0);
}
