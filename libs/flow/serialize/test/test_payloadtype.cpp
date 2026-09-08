// A node's PAYLOAD TYPES on disk (ADR-0022): the `types` section, what its absence means, and what
// an unknown type key does.
//
// The section is what makes ONE `constant` kind able to emit any type — the reason the format needs
// no version bump is here too: `types` is optional, and absent means the factory's preset stands,
// exactly as an absent `params` means the declared defaults do.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/nodes/cast.h>
#include <lain/flow/nodes/constant.h>
#include <lain/flow/porttype.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>

using lain::core::Factory;
using lain::data::Value;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	// The app's registrations, standing in for flowview's. flow core registers no payload type and
	// no conversion — which of each exist is the app's decision.
	void registerTypes()
	{
		registerPortType<int>("Int");
		registerPortType<float>("Float");
		registerPortType<std::string>("String");
	}

	ValueCodecs codecs()
	{
		ValueCodecs c;
		c.registerType<int>("int");
		c.registerType<float>("float");
		c.registerType<std::string>("string");
		return c;
	}

	Factory<Node> factory()
	{
		Factory<Node> f;
		f.registerType<GroupInputNode>("groupInput");
		f.registerType<GroupOutputNode>("groupOutput");
		// ONE Constant kind, preset to Int — the type it actually emits travels in `types`.
		f.registerType<ConstantNode>("constant", std::cref(portType<int>()));
		f.registerType<CastNode>("cast", std::cref(portType<int>()), std::cref(portType<float>()));
		return f;
	}

	// The `types` object a saved node carries, or nullptr.
	const Value* typesOf(const Value& document, std::size_t node)
	{
		const Value::Array* nodes = document.find("nodes")->asArray();
		return (*nodes)[node].find("types");
	}

	// Value::find is const-only, so a test that EDITS a document walks the object itself. Both
	// helpers exist to hand-make the two documents a save cannot produce: one with no `types`
	// section (what every document written before payload types looks like) and one naming a type
	// this build does not register.
	Value* findMutable(Value& value, const std::string& key)
	{
		Value::Object* object = value.asObject();
		if (object == nullptr)
			return nullptr;
		for (auto& entry : *object)
		{
			if (entry.first == key)
				return &entry.second;
		}
		return nullptr;
	}

	bool eraseKey(Value& value, const std::string& key)
	{
		Value::Object* object = value.asObject();
		if (object == nullptr)
			return false;
		for (auto it = object->begin(); it != object->end(); ++it)
		{
			if (it->first == key)
			{
				object->erase(it);
				return true;
			}
		}
		return false;
	}

	// The one non-boundary node of a loaded graph.
	const ConstantNode& theConstant(const Graph& graph)
	{
		for (const NodeId id : graph.nodeIds())
		{
			if (const auto* constant = dynamic_cast<const ConstantNode*>(&graph.node(id)))
				return *constant;
		}
		throw std::runtime_error("no constant in the loaded graph");
	}
} // namespace

TEST_CASE("a node's payload type is written and read back", "[flow-serialize][payload]")
{
	registerTypes();
	const Factory<Node> f = factory();

	Graph graph;
	const NodeId id = graph.add(constantOf(2.5f));
	REQUIRE(graph.node(id).output(0).type() == typeid(float));

	const Value document = toValue(graph, f, codecs(), {});

	// It saves under the ONE kind — not under a per-type key — with its type beside it.
	const Value::Array* nodes = document.find("nodes")->asArray();
	std::size_t constantAt = 0;
	for (std::size_t i = 0; i < nodes->size(); ++i)
	{
		if (*(*nodes)[i].find("kind")->asString() == "constant")
			constantAt = i;
	}
	const Value* types = typesOf(document, constantAt);
	REQUIRE(types != nullptr);
	CHECK(*types->find("value")->asString() == "Float");

	LoadResult loaded = fromValue(document, f, codecs(), nullptr);
	CHECK(loaded.issues.empty());
	const ConstantNode& constant = theConstant(loaded.graph);
	// The DOCUMENT wins over the factory's Int preset — declarations and param value alike.
	CHECK(constant.output(0).type() == typeid(float));
	CHECK(constant.value<float>() == 2.5f);
}

TEST_CASE("a document with no types section keeps the factory's preset", "[flow-serialize][payload]")
{
	// The compatibility contract, and why no schema version bump was needed: this is what every
	// document written before payload types existed looks like.
	registerTypes();
	const Factory<Node> f = factory();

	Graph graph;
	graph.add(constantOf(7));
	Value document = toValue(graph, f, codecs(), {});

	Value::Array* nodes = findMutable(document, "nodes")->asArray();
	std::size_t erased = 0;
	for (Value& node : *nodes)
	{
		if (eraseKey(node, "types"))
			++erased;
	}
	REQUIRE(erased == 1);

	LoadResult loaded = fromValue(document, f, codecs(), nullptr);
	CHECK(loaded.issues.empty());
	const ConstantNode& constant = theConstant(loaded.graph);
	CHECK(constant.output(0).type() == typeid(int)); // the preset
	CHECK(constant.value<int>() == 7);				 // and its param still round-tripped
}

TEST_CASE("an unknown port type keeps the preset and is reported", "[flow-serialize][payload]")
{
	// A build that does not register a type must not lose the node and its edges over it — the same
	// judgement the dynamic-pin replay makes. Reported, so it is not silent.
	registerTypes();
	const Factory<Node> f = factory();

	Graph graph;
	graph.add(constantOf(2.5f));
	Value document = toValue(graph, f, codecs(), {});

	Value::Array* nodes = findMutable(document, "nodes")->asArray();
	for (Value& node : *nodes)
	{
		if (Value* types = findMutable(node, "types"))
			types->set("value", Value(std::string{"Voxel"}));
	}

	LoadResult loaded = fromValue(document, f, codecs(), nullptr);
	CHECK_FALSE(loaded.issues.empty());
	const ConstantNode& constant = theConstant(loaded.graph);
	CHECK(constant.output(0).type() == typeid(int)); // kept its preset rather than vanishing
}

TEST_CASE("a payload-typed document is byte-idempotent across a round trip", "[flow-serialize][payload]")
{
	registerTypes();
	const Factory<Node> f = factory();

	Graph graph;
	graph.add(constantOf(std::string{"hello"}));
	const Value once = toValue(graph, f, codecs(), {});
	LoadResult loaded = fromValue(once, f, codecs(), nullptr);
	REQUIRE(loaded.issues.empty());
	const Value twice = toValue(loaded.graph, f, codecs(), {});

	CHECK(once == twice);
}

TEST_CASE("a node with two payload types round-trips both", "[flow-serialize][payload]")
{
	// A Cast is the node that made the mechanism necessary, and the one where the ORDER the loader
	// applies payload types in could have mattered. It does not, because a pair with no conversion
	// is representable rather than refused — so a document naming Path -> String cannot come back as
	// something else just because `from` was applied while `to` was still the factory's preset.
	registerTypes();
	const Factory<Node> f = factory();

	Graph graph;
	graph.add<CastNode>(portType<std::string>(), portType<int>());
	const Value document = toValue(graph, f, codecs(), {});

	const Value::Array* nodes = document.find("nodes")->asArray();
	const Value* types = nullptr;
	for (const Value& node : *nodes)
	{
		if (*node.find("kind")->asString() == "cast")
			types = node.find("types");
	}
	REQUIRE(types != nullptr);
	CHECK(*types->find("from")->asString() == "String");
	CHECK(*types->find("to")->asString() == "Int");

	LoadResult loaded = fromValue(document, f, codecs(), nullptr);
	CHECK(loaded.issues.empty());
	for (const NodeId id : loaded.graph.nodeIds())
	{
		if (const auto* cast = dynamic_cast<const CastNode*>(&loaded.graph.node(id)))
		{
			CHECK(cast->input(0).type() == typeid(std::string));
			CHECK(cast->output(0).type() == typeid(int));
		}
	}
	CHECK(toValue(loaded.graph, f, codecs(), {}) == document);
}
