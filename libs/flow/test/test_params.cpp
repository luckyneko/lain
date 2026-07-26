// Unit: node parameters — typed, non-connectable configuration slots declared with
// addParam<T>, read in compute() via param(i).get<T>(), and edited by the adapter via
// param(i).set<T>(). Pure CPU, no device.

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <typeindex>

using namespace lain::flow;

// A node with three typed params; compute() copies the int one onto an output port so a
// test can confirm the value the node actually read.
class ConfigNode : public Node
{
public:
	ConfigNode()
		: Node("Config")
	{
		m_count = addParam<int>("count", 3);
		m_scale = addParam<float>("scale", 1.5f);
		m_label = addParam<std::string>("label", "hi");
		m_out = addOutput<int>("readCount");
	}

	void compute() override
	{
		output(m_out).set<int>(param(m_count).get<int>()); // read a param like a real node
	}

private:
	PortIndex m_count = 0;
	PortIndex m_scale = 0;
	PortIndex m_label = 0;
	PortIndex m_out = 0;
};

TEST_CASE("a node declares typed params with defaults", "[param]")
{
	const ConfigNode node;
	REQUIRE(node.paramCount() == 3);

	REQUIRE(node.param(0).name() == "count");
	REQUIRE(node.param(0).type() == std::type_index(typeid(int)));
	REQUIRE(node.param(0).get<int>() == 3);
	REQUIRE(node.param(0).holds<int>());
	REQUIRE_FALSE(node.param(0).holds<float>());

	REQUIRE(node.param(1).get<float>() == 1.5f);
	REQUIRE(node.param(2).get<std::string>() == "hi");
}

TEST_CASE("compute reads a param's default value", "[param]")
{
	Graph graph;
	const NodeId id = graph.add<ConfigNode>();
	SerialScheduler{}.evaluate(graph, id);
	REQUIRE(graph.node(id).output(0).get<int>() == 3);
}

TEST_CASE("editing a param changes what compute reads", "[param]")
{
	Graph graph;
	const NodeId id = graph.add<ConfigNode>();

	graph.node(id).param(0).set<int>(7); // the adapter edits via the accessor
	SerialScheduler{}.evaluate(graph, id);
	REQUIRE(graph.node(id).output(0).get<int>() == 7);
}

TEST_CASE("a param describes its value as text", "[param]")
{
	const ConfigNode node;
	REQUIRE(node.param(0).describe() == "3"); // int via the PortType meta::toString bridge
}
