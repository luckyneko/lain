// Params round-trip through the value-serializer registry (ValueCodecs) — the piece the graph walk
// will lean on for every node's config. GPU-free node, plain Graph, no live GUI.

#include "lain/flow/serialize/valuecodecs.h"

#include <lain/data/value.h>
#include <lain/flow/node.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

using lain::data::Value;
using lain::flow::serialize::paramFromValue;
using lain::flow::serialize::paramToValue;
using lain::flow::serialize::ValueCodecs;

// A GPU-free node with a few typed params — a param carries config (radius, path, count), the shape
// the value-serializer registry round-trips.
class ParamNode : public lain::flow::Node
{
public:
	ParamNode()
		: Node("params")
	{
		addParam<float>("radius", 2.0f);
		addParam<std::string>("path", "in.png");
		addParam<int>("count", 5);
	}

	void compute() override {}
};

static ValueCodecs builtinCodecs()
{
	ValueCodecs codecs;
	codecs.registerType<float>("float");
	codecs.registerType<int>("int");
	codecs.registerType<std::string>("string");
	return codecs;
}

TEST_CASE("a param serialises to { name, type, value }", "[flow-serialize]")
{
	const ValueCodecs codecs = builtinCodecs();
	ParamNode node;

	const auto v = paramToValue(node.param(0), codecs); // "radius", float 2.0
	REQUIRE(v.has_value());
	REQUIRE(*v->find("name")->asString() == "radius");
	REQUIRE(*v->find("type")->asString() == "float"); // codec key, a cross-check
	REQUIRE(v->find("value")->asDouble() == 2.0);
}

TEST_CASE("params round-trip through the registry (declared type authoritative)", "[flow-serialize]")
{
	const ValueCodecs codecs = builtinCodecs();

	ParamNode source;
	source.param(0).set<float>(9.5f);
	source.param(1).set<std::string>("out.png");
	source.param(2).set<int>(42);

	std::vector<Value> stored;
	for (lain::flow::PortIndex i = 0; i < source.paramCount(); ++i)
		stored.push_back(*paramToValue(source.param(i), codecs));

	ParamNode target; // fresh defaults; the stored values overwrite them
	for (std::size_t i = 0; i < stored.size(); ++i)
		REQUIRE(paramFromValue(target.param(i), stored[i], codecs));

	REQUIRE(target.param(0).get<float>() == 9.5f);
	REQUIRE(target.param(1).get<std::string>() == "out.png");
	REQUIRE(target.param(2).get<int>() == 42);
}

TEST_CASE("an unregistered param type is nullopt / false, not a crash", "[flow-serialize]")
{
	const ValueCodecs empty; // nothing registered
	ParamNode node;

	REQUIRE_FALSE(paramToValue(node.param(0), empty).has_value());
	REQUIRE_FALSE(paramFromValue(node.param(0), Value::object(), empty));
}
