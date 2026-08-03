// Unit: node parameters — typed, non-connectable configuration slots declared with addParam<T>,
// read in compute() through the PortId the declaration returned, and edited by the adapter while
// iterating by position. Pure CPU, no device.

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/portvalue.h"
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

	Node& node = graph.node(id);
	REQUIRE(node.setParam(node.param(0).id(), 7)); // the adapter edits through the one seam
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

// --- the mutation seam (M6 step 3) ------------------------------------------

TEST_CASE("setParam type-checks against the DECLARED type", "[param]")
{
	// "The type is the schema": the declaration is authoritative, so a value of any other type is
	// refused rather than quietly retyping the param out from under compute().
	ConfigNode node;
	const PortId count = node.countId();

	REQUIRE(node.setParam(count, 7));
	REQUIRE(node.param(count).get<int>() == 7);

	REQUIRE_FALSE(node.setParam(count, 1.5f));				  // wrong type
	REQUIRE_FALSE(node.setParam(count, std::string("nope"))); // ... whatever it is
	REQUIRE_FALSE(node.setParam(count, PortValue{}));		  // an empty value is not "clear the param"
	REQUIRE(node.param(count).get<int>() == 7);				  // a refusal changes NOTHING
	REQUIRE(node.param(count).holds<int>());				  // ... including the declared type
}

TEST_CASE("setParam refuses an id that names no param of this node", "[param]")
{
	ConfigNode node;
	REQUIRE_FALSE(node.setParam(PortId{}, 1));			 // the null sentinel
	REQUIRE_FALSE(node.setParam(PortId{9999}, 1));		 // an id from nowhere
	REQUIRE_FALSE(node.setParam(node.outId(), 1));		 // a PORT's id — ports are not params
	REQUIRE(node.param(node.countId()).get<int>() == 3); // still the declared default
}

TEST_CASE("setParam commits and invalidates as ONE operation", "[param]")
{
	// The reason the seam exists. A param is recipe, so a change to one must recompute the node —
	// and when the write and the invalidation are separate calls, the second one gets forgotten.
	Graph graph;
	const NodeId id = graph.add<ConfigNode>();
	Node& node = graph.node(id);

	SerialScheduler{}.evaluate(graph, id);
	REQUIRE(node.output(0).get<int>() == 3);
	REQUIRE_FALSE(node.dirty()); // the run cleared it

	REQUIRE(node.setParam(node.param(0).id(), 12));
	REQUIRE(node.dirty()); // ... and the edit dirtied it again, with no separate markDirty

	SerialScheduler{}.evaluate(graph, id);
	REQUIRE(node.output(0).get<int>() == 12);

	// A REFUSED edit must not dirty the node either — it changed nothing, so there is nothing to
	// recompute, and a spurious dirty would silently cost a re-run of the downstream cone.
	node.clearDirty();
	REQUIRE_FALSE(node.setParam(node.param(0).id(), 1.5f));
	REQUIRE_FALSE(node.dirty());
}

TEST_CASE("an already type-erased value commits through the same seam", "[param]")
{
	// The path serialization and the Inspector take: they hold a PortValue decoded or edited at
	// runtime, not a compile-time T, and must not need a second entry point.
	ConfigNode node;
	PortValue value;
	value.set<float>(2.5f);
	REQUIRE(node.setParam(node.scaleId(), value)); // the non-template overload
	REQUIRE(node.param(node.scaleId()).get<float>() == 2.5f);

	// The caller's copy still holds the payload — a commit shares it, never consumes it.
	REQUIRE(value.get<float>() == 2.5f);
}
