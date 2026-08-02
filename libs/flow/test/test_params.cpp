// Unit: node parameters — typed, non-connectable configuration slots declared with addParam<T>,
// read in compute() through the PortId the declaration returned, and edited by the adapter while
// iterating by position. Pure CPU, no device.

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
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

	// Exposed so a test can check what the declarations handed back (a node would normally keep
	// these to itself).
	PortId countId() const { return m_count; }
	PortId scaleId() const { return m_scale; }
	PortId outId() const { return m_out; }

private:
	PortId m_count;
	PortId m_scale;
	PortId m_label;
	PortId m_out;
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

// --- identity (M6 step 2) ---------------------------------------------------

TEST_CASE("a param is addressable by the id its declaration returned", "[param]")
{
	// A param declaration hands back the same kind of handle a port declaration does, so a node
	// stores it and reads it back rather than remembering a position. There is no dynamic param
	// today; the point is that adding one later cannot invalidate what nodes already hold.
	const ConfigNode node;

	REQUIRE(node.param(node.countId()).name() == "count");
	REQUIRE(node.param(node.countId()).get<int>() == 3);
	REQUIRE(node.param(node.scaleId()).get<float>() == 1.5f);
	REQUIRE(node.param(node.countId()).id() == node.countId());

	// Position and identity agree here only because nothing has moved — they are different questions.
	REQUIRE(&node.param(node.countId()) == &node.param(std::size_t{0}));
}

TEST_CASE("params and ports draw ids from one counter, so the two never collide", "[param]")
{
	// Both are things the node DECLARES, and both are reached by PortId — so a param id must never
	// equal a port id on the same node, or handing one to input()/output() would silently find the
	// wrong thing instead of finding nothing.
	const ConfigNode node;

	REQUIRE(node.countId() != node.outId());
	REQUIRE(node.findOutput(node.countId()) == nullptr); // a param id names no port
	REQUIRE(node.findInput(node.countId()) == nullptr);
	REQUIRE(node.findParam(node.outId()) == nullptr); // ... and a port id names no param

	std::set<std::uint32_t> ids;
	for (std::size_t i = 0; i < node.paramCount(); ++i)
		ids.insert(node.param(i).id().value());
	for (std::size_t i = 0; i < node.outputCount(); ++i)
		ids.insert(node.output(i).id().value());
	REQUIRE(ids.size() == node.paramCount() + node.outputCount()); // all distinct
}
