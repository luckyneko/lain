// The APP's half of payload types (ADR-0022): which conversions this app registers and what they
// mean, and the compatibility contract for the five keys that `constant` replaced.
//
// Compiles the production scene.cpp / graphio.cpp, so these are the real registrations the binary
// runs — not a copy that could drift from them.

#include "graphio.h"
#include "scene.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/nodes/constant.h>
#include <lain/flow/porttype.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/image/image.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

using lain::core::Factory;
using lain::data::Value;
using namespace lain::flow;

namespace
{
	Factory<Node> sceneFactory()
	{
		flowview::registerSceneSerialization();
		Factory<Node> factory;
		flowview::registerExampleNodes(factory, 8);
		return factory;
	}

	// A value of T, erased — the shape convertValue takes.
	template <typename T>
	PortValue valued(T value)
	{
		PortValue slot;
		slot.set<T>(std::move(value));
		return slot;
	}
} // namespace

TEST_CASE("a constant of any payload type saves under the one `constant` kind", "[flowview][payload]")
{
	// THE thing the legacy keys must not break. Factory::keyOf maps one class to one key, so if
	// constInt / constFloat / … were registered the typed way, keyOf would answer whichever
	// registered last and a Float constant would save as "constInt" carrying a `types` section
	// contradicting its own kind — a document that still loads, and lies.
	const Factory<Node> factory = sceneFactory();

	Graph graph;
	graph.add(constantOf(2.5f));
	const Value document = lain::flow::serialize::toValue(graph, factory, flowview::sceneCodecs(), {});

	const Value::Array* nodes = document.find("nodes")->asArray();
	bool sawConstant = false;
	for (const Value& node : *nodes)
	{
		const std::string kind = *node.find("kind")->asString();
		// Never one of the five keys `constant` replaced: nothing saves under them.
		CHECK(kind != "constInt");
		CHECK(kind != "constBool");
		CHECK(kind != "constFloat");
		CHECK(kind != "constPath");
		CHECK(kind != "constString");
		if (kind == "constant")
		{
			sawConstant = true;
			REQUIRE(node.find("types") != nullptr);
			CHECK(*node.find("types")->find("value")->asString() == "Float");
		}
	}
	CHECK(sawConstant);
}

TEST_CASE("the five keys `constant` replaced still create the node they used to", "[flowview][payload]")
{
	// A document written before payload types names one of these. It must open with the SAME type
	// it had, which is what makes this an additive format change rather than a schema version.
	const Factory<Node> factory = sceneFactory();

	struct Legacy
	{
		const char* key;
		const std::type_info& type;
	};
	const Legacy legacy[] = {
		{"constInt", typeid(int)},
		{"constBool", typeid(bool)},
		{"constFloat", typeid(float)},
		{"constPath", typeid(std::filesystem::path)},
		{"constString", typeid(std::string)},
	};

	for (const Legacy& entry : legacy)
	{
		std::unique_ptr<Node> node = factory.create(entry.key);
		REQUIRE(node != nullptr);
		CHECK(node->output(0).type() == entry.type);
		CHECK(node->payloadTypes().size() == 1);
	}

	// And the new key is there beside them, at its Int preset.
	std::unique_ptr<Node> fresh = factory.create("constant");
	REQUIRE(fresh != nullptr);
	CHECK(fresh->output(0).type() == typeid(int));
}

TEST_CASE("float to int truncates toward zero", "[flowview][payload][conversion]")
{
	// STATED, not inherited: a Cast is the user asking for a lossy conversion, so which loss it is
	// has to be a decision someone made. Rounding is a different operation and belongs to a numeric
	// node that says so.
	flowview::registerSceneSerialization(); // port types AND conversions, as the binary does

	CHECK(convertValue(valued(2.9f), portType<int>()).get<int>() == 2);
	CHECK(convertValue(valued(-2.9f), portType<int>()).get<int>() == -2);
	CHECK(convertValue(valued(2.0f), portType<int>()).get<int>() == 2);

	// The other direction is exact, and is the pair that unblocked driving a float setting from a
	// loop's int `index`.
	CHECK(convertValue(valued(5), portType<float>()).get<float>() == 5.0f);
}

TEST_CASE("text converts only when the whole string is a number", "[flowview][payload][conversion]")
{
	// The strict parse clibinders.cpp already applies to a cli value — trailing junk is not a
	// number. A graph quietly reading 12 out of "12x" is worse than a Cast that produces nothing.
	flowview::registerSceneSerialization(); // port types AND conversions, as the binary does

	CHECK(convertValue(valued(std::string{"12"}), portType<int>()).get<int>() == 12);
	CHECK(convertValue(valued(std::string{"-4"}), portType<int>()).get<int>() == -4);
	CHECK(convertValue(valued(std::string{"12x"}), portType<int>()).empty());
	CHECK(convertValue(valued(std::string{"hello"}), portType<int>()).empty());
	CHECK(convertValue(valued(std::string{""}), portType<int>()).empty());

	CHECK(convertValue(valued(std::string{"1.5"}), portType<float>()).get<float>() == 1.5f);
	CHECK(convertValue(valued(std::string{"1.5f"}), portType<float>()).empty());

	// A path IS text, both ways and without loss.
	CHECK(convertValue(valued(std::filesystem::path{"/tmp/x.png"}), portType<std::string>()).get<std::string>() == "/tmp/x.png");
	CHECK(convertValue(valued(std::string{"/tmp/x.png"}), portType<std::filesystem::path>()).get<std::filesystem::path>() ==
		  std::filesystem::path{"/tmp/x.png"});
}

TEST_CASE("the conversions this app registers are exactly the ones it decided on", "[flowview][payload][conversion]")
{
	flowview::registerSceneSerialization(); // port types AND conversions, as the binary does

	// bool <-> int is deliberately ABSENT: what `2 -> true` should mean is a decision nothing is
	// asking for, and an absent conversion is a menu entry that never appears rather than a wrong
	// answer nobody notices.
	CHECK_FALSE(conversionRegistered(typeid(bool), typeid(int)));
	CHECK_FALSE(conversionRegistered(typeid(int), typeid(bool)));
	// Nor anything into or out of an image: a picture is not a number.
	CHECK_FALSE(conversionRegistered(typeid(int), typeid(lain::image::Image)));

	// What a host offers as a Cast's target for an int, in the order it offers them — the palette's
	// per-pair entries are generated from exactly this, so they cannot drift from what can be done.
	const std::vector<const PortType*> fromInt = conversionsFrom(typeid(int));
	std::vector<std::string> keys;
	for (const PortType* type : fromInt)
		keys.push_back(portTypeKey(type->index));
	CHECK(keys == std::vector<std::string>{"Float", "String"});

	// And every registered pair names two registered types, or the menu built from it would show a
	// blank entry that creates a node nothing can save.
	for (const auto& pair : registeredConversions())
	{
		CHECK_FALSE(portTypeKey(pair.first->index).empty());
		CHECK_FALSE(portTypeKey(pair.second->index).empty());
	}
}
