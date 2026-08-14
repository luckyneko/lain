// Serializing a MAP (M8 slice 5, ADR-0014). A map stores its recipe as a nested body, like an
// inline group — plus, uniquely among the group kinds, its OWN outer ports.
//
// That exception is the whole point of this file. M5 established that a group's ports are never
// stored because edit::syncGroupPorts re-derives them from the inner boundary; a map's mirroring is
// under-determined by exactly one bit per input pin (an inner T can be mirrored as vector<T> to
// SPLIT or as T to BROADCAST), so derivation alone would silently turn every broadcast back into a
// split. What is stored is the port's TYPE, not a flag: the type is the mode.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using lain::core::Factory;
using lain::data::Value;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	using Ints = std::vector<int>;

	// A map can only lift a type whose LIST FORM is registered — registering vector<int> is what
	// makes int mappable, and it is also what lets a collection port name its type on disk.
	void registerTypes()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		registerPortType<int>("Int");
		registerPortType<Ints>("ListOfInt");
	}

	struct MakeInts : Node
	{
		PortId out;
		MakeInts()
			: Node("MakeInts")
		{
			out = addOutput<Ints>("items");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(Ints{1, 2, 3});
		}
	};

	struct ConstInt : Node
	{
		PortId out;
		ConstInt()
			: Node("ConstInt")
		{
			addParam<int>("value", 0);
			out = addOutput<int>("value");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(param(0).get<int>());
		}
	};

	// item + offset, so a test can tell a broadcast offset from a split one by the values alone.
	struct AddTwo : Node
	{
		PortId a, b, out;
		AddTwo()
			: Node("AddTwo")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(a).get<int>() + evaluation.input(b).get<int>());
		}
	};

	// Without a codec for int the param is skipped on save — best-effort by design — and the reloaded
	// ConstInt would quietly fall back to its declared default. Registering it is what makes the
	// round-trip assertions below about the MAP rather than about a missing codec.
	ValueCodecs intCodecs()
	{
		ValueCodecs codecs;
		codecs.registerType<int>("int");
		return codecs;
	}

	Factory<Node> makeFactory()
	{
		Factory<Node> factory;
		factory.registerType<MapNode>("map");
		factory.registerType<MakeInts>("makeInts");
		factory.registerType<ConstInt>("constInt");
		factory.registerType<AddTwo>("addTwo");
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		return factory;
	}

	// data::Value::find is const-only — a document is read in production, never edited — so a test
	// that deliberately corrupts one reaches through the non-const object accessor.
	Value* mutableFind(Value& value, std::string_view key)
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

	void eraseKey(Value& value, std::string_view key)
	{
		Value::Object* object = value.asObject();
		if (object == nullptr)
			return;
		for (auto it = object->begin(); it != object->end(); ++it)
		{
			if (it->first == key)
			{
				object->erase(it);
				return;
			}
		}
	}

	Connection wire(Graph& g, NodeId from, PortId fromPin, NodeId to, std::size_t toIndex)
	{
		return g.connect(PortAddress{from, fromPin}, PortAddress{to, g.node(to).input(toIndex).id()});
	}

	Connection wire(Graph& g, NodeId from, std::size_t fromIndex, NodeId to, PortId toPin)
	{
		return g.connect(PortAddress{from, g.node(from).output(fromIndex).id()}, PortAddress{to, toPin});
	}

	// items (split) + offset (broadcast) -> AddTwo -> result. The offset pin is deliberately mirrored
	// UN-lifted, which is the choice this file exists to protect.
	NodeId buildMappedDocument(Graph& graph)
	{
		const NodeId id = graph.add<MapNode>();
		auto& map = static_cast<MapNode&>(graph.node(id));

		const PortId itemPin = map.inner().boundaryInputNode().addBoundary<int>("item");
		const PortId offsetPin = map.inner().boundaryInputNode().addBoundary<int>("offset");
		const PortId resultPin = map.inner().boundaryOutputNode().addBoundary<int>("result");
		const NodeId body = map.inner().add<AddTwo>();
		REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), itemPin, body, 0) == Connection::Ok);
		REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), offsetPin, body, 1) == Connection::Ok);
		REQUIRE(wire(map.inner(), body, 0, map.inner().boundaryOutputNode().id(), resultPin) == Connection::Ok);

		map.exposePort(Port::Direction::Input, *map.inner().boundaryInputNode().findOutput(itemPin));
		map.exposeBroadcast(Port::Direction::Input, *map.inner().boundaryInputNode().findOutput(offsetPin));
		map.exposePort(Port::Direction::Output, *map.inner().boundaryOutputNode().findInput(resultPin));

		const NodeId items = graph.add<MakeInts>();
		const NodeId offset = graph.add<ConstInt>();
		REQUIRE(graph.node(offset).setParam<int>(graph.node(offset).param(0).id(), 100));
		REQUIRE(graph.connect(items, 0, id, 0) == Connection::Ok);
		REQUIRE(graph.connect(offset, 0, id, 1) == Connection::Ok);
		return id;
	}
} // namespace

TEST_CASE("a map round-trips, and its broadcast pin stays a broadcast", "[flow][serialize][map]")
{
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = intCodecs();

	Graph original;
	const NodeId mapId = buildMappedDocument(original);

	// It computes item + 100 for every element before the round-trip...
	{
		Evaluation evaluation{original};
		SerialScheduler{}.run(original, evaluation);
		const Ints expected{101, 102, 103};
		REQUIRE(evaluation.value(PortAddress{mapId, original.node(mapId).output(0).id()}).get<Ints>() == expected);
	}

	const Value document = toValue(original, factory, codecs, {});
	LoadResult loaded = fromValue(document, factory, codecs);
	REQUIRE(loaded.graph.nodeCount() == original.nodeCount());

	// ...and the same after it. If the broadcast pin had come back lifted, the offset would be a
	// vector<int> port fed by an int producer — the edge would not reconnect and the map would
	// suppress, so this assertion is the honest end-to-end check.
	NodeId reloadedMap;
	for (const NodeId id : loaded.graph.nodeIds())
	{
		if (loaded.graph.node(id).innerGraph() != nullptr)
			reloadedMap = id;
	}
	REQUIRE(reloadedMap != NodeId{});

	const Node& map = loaded.graph.node(reloadedMap);
	REQUIRE(map.inputCount() == 2);
	REQUIRE(map.input(0).type() == std::type_index(typeid(Ints))); // split: lifted
	REQUIRE(map.input(1).type() == std::type_index(typeid(int)));  // broadcast: un-lifted
	REQUIRE(map.output(0).type() == std::type_index(typeid(Ints)));

	Evaluation evaluation{loaded.graph};
	SerialScheduler{}.run(loaded.graph, evaluation);
	const Ints expected{101, 102, 103};
	REQUIRE(evaluation.value(PortAddress{reloadedMap, map.output(0).id()}).get<Ints>() == expected);
}

TEST_CASE("a map document is byte-idempotent", "[flow][serialize][map]")
{
	// save -> load -> save must produce the identical document, or a round-trip quietly rewrites a
	// user's file. It is also the sharpest check that nothing about the map is being re-derived
	// differently on the way back in.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = intCodecs();

	Graph original;
	buildMappedDocument(original);

	const Value first = toValue(original, factory, codecs, {});
	LoadResult loaded = fromValue(first, factory, codecs);
	const Value second = toValue(loaded.graph, factory, codecs, {});

	REQUIRE(loaded.issues.empty());
	REQUIRE(first == second);
}

TEST_CASE("a map port whose pin has vanished is reported, not silently kept", "[flow][serialize][map]")
{
	// Rectification, the same courtesy a linked group's interface cache gets: the document says the
	// map had a port, the rebuilt interior has no such pin, so the port is dropped and the user is
	// told rather than finding wiring missing later.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = intCodecs();

	Graph original;
	const NodeId mapId = buildMappedDocument(original);
	Value document = toValue(original, factory, codecs, {});

	// Add a port to the stored interface that the body does not contain.
	Value* nodes = mutableFind(document, "nodes");
	REQUIRE(nodes != nullptr);
	bool patched = false;
	for (Value& nodeV : *nodes->asArray())
	{
		const Value* kind = nodeV.find("kind");
		if (kind == nullptr || kind->asString() == nullptr || *kind->asString() != "map")
			continue;
		Value* interfaceV = mutableFind(nodeV, "interface");
		REQUIRE(interfaceV != nullptr);
		Value* inputs = mutableFind(*interfaceV, "inputs");
		REQUIRE(inputs != nullptr);
		Value ghost = Value::object();
		ghost.set("name", Value(std::string{"ghost"}));
		ghost.set("type", Value(std::string{"ListOfInt"}));
		inputs->push(std::move(ghost));
		patched = true;
	}
	REQUIRE(patched);

	LoadResult loaded = fromValue(document, factory, codecs);
	REQUIRE_FALSE(loaded.issues.empty());

	bool reported = false;
	for (const LoadIssue& issue : loaded.issues)
	{
		if (issue.message.find("ghost") != std::string::npos)
			reported = true;
	}
	REQUIRE(reported);

	// The real ports are unaffected — a bad entry costs its own port, not the node's.
	NodeId reloadedMap;
	for (const NodeId id : loaded.graph.nodeIds())
	{
		if (loaded.graph.node(id).innerGraph() != nullptr)
			reloadedMap = id;
	}
	REQUIRE(loaded.graph.node(reloadedMap).inputCount() == 2);
	(void)mapId;
}

TEST_CASE("a map with no stored interface mirrors at the default", "[flow][serialize][map]")
{
	// Forward compatibility with a hand-written document, and the fallback when the section is
	// missing for any reason: every pin lifts, which is the mode a fresh map is authored in.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = intCodecs();

	Graph original;
	buildMappedDocument(original);
	Value document = toValue(original, factory, codecs, {});

	Value* nodes = mutableFind(document, "nodes");
	for (Value& nodeV : *nodes->asArray())
	{
		const Value* kind = nodeV.find("kind");
		if (kind != nullptr && kind->asString() != nullptr && *kind->asString() == "map")
			eraseKey(nodeV, "interface");
	}

	LoadResult loaded = fromValue(document, factory, codecs);
	NodeId reloadedMap;
	for (const NodeId id : loaded.graph.nodeIds())
	{
		if (loaded.graph.node(id).innerGraph() != nullptr)
			reloadedMap = id;
	}

	const Node& map = loaded.graph.node(reloadedMap);
	REQUIRE(map.inputCount() == 2);
	REQUIRE(map.input(0).type() == std::type_index(typeid(Ints)));
	REQUIRE(map.input(1).type() == std::type_index(typeid(Ints))); // the broadcast choice is gone: lifted
}
