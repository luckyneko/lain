// Increment 3b: dynamic boundary pins replay through the port-type registry (edges reconnect by
// name), and the adapter-owned "editor" section round-trips opaquely, re-keyed to the loaded ids.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using lain::core::Factory;
using lain::data::Value;
using lain::flow::Connection;
using lain::flow::Graph;
using lain::flow::GroupInputNode;
using lain::flow::Node;
using lain::flow::NodeId;
using lain::flow::Port;
using lain::flow::PortId;
using lain::flow::serialize::EditorData;
using lain::flow::serialize::fromValue;
using lain::flow::serialize::LoadResult;
using lain::flow::serialize::toValue;
using lain::flow::serialize::ValueCodecs;

class IntSink : public Node
{
public:
	IntSink()
		: Node("Sink")
	{
		addInput<int>("in");
	}
	void compute() override {}
};

static Factory<Node> boundaryFactory()
{
	Factory<Node> factory;
	factory.registerType<GroupInputNode>("groupInput");
	factory.registerType<IntSink>("sink");
	return factory;
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

TEST_CASE("dynamic boundary pins replay and edges reconnect by name", "[flow-serialize]")
{
	lain::flow::registerPortType<int>("Int"); // gives an int pin a stable port-type key
	const Factory<Node> factory = boundaryFactory();
	const ValueCodecs codecs;

	Graph graph;
	const NodeId group = graph.add<GroupInputNode>();
	const NodeId sink = graph.add<IntSink>();
	auto& groupNode = static_cast<GroupInputNode&>(graph.node(group));
	const PortId pin = groupNode.addBoundary<int>("a"); // a runtime output pin "a"
	REQUIRE(pin != PortId{});
	REQUIRE(graph.connect({group, pin}, {sink, graph.node(sink).input(0).id()}) == Connection::Ok);

	const Value doc = toValue(graph, factory, codecs);

	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE(result.clean());
	REQUIRE(result.graph.nodeCount() == 2);
	REQUIRE(result.graph.edges().size() == 1); // the edge reconnected by pin name

	const Node* loadedGroup = nodeNamed(result.graph, "GroupInput");
	REQUIRE(loadedGroup != nullptr);
	REQUIRE(loadedGroup->hasPortNamed(Port::Direction::Output, "a")); // the pin replayed
}

TEST_CASE("the editor section round-trips opaquely, re-keyed to loaded ids", "[flow-serialize]")
{
	const Factory<Node> factory = boundaryFactory();
	const ValueCodecs codecs;

	Graph graph;
	const NodeId node = graph.add<IntSink>();

	EditorData editor; // the adapter's per-node metadata, keyed by live NodeId
	Value meta = Value::object();
	meta.set("x", Value(10.0));
	meta.set("y", Value(20.0));
	editor[node] = meta;

	const Value doc = toValue(graph, factory, codecs, editor);
	REQUIRE(doc.find("editor") != nullptr);

	const LoadResult result = fromValue(doc, factory, codecs);
	REQUIRE(result.clean());
	REQUIRE(result.editor.size() == 1);

	const NodeId loadedId = result.graph.nodeIds().front(); // the one node's fresh id
	REQUIRE(result.editor.count(loadedId) == 1);			// re-keyed to it
	REQUIRE(result.editor.at(loadedId).find("x")->asDouble() == 10.0);
}
