// Serializing a LOOP (M11 slice 5, ADR-0021). A loop stores its recipe as a nested body like an
// inline group, plus one `loop` section — and, unlike a map, NOT its own ports.
//
// Two things live in that section, and neither is a nicety:
//
//   * the CARRY PAIRING, which is the one thing about an interior that cannot be derived from it: a
//     carried Image and an invariant Image are the same type, so no declaration encodes which is
//     which. The ports themselves stay derived (edit::syncGroupPorts), because the pairing changes
//     what the engine does BETWEEN iterations and never what the face looks like.
//
//   * the two RESERVED PIN NAMES. A reserved pin is static — never written as a pin, never replayed
//     — and the loader REPLACES a loop's whole interior with the body it read, so the pins the
//     constructor made are destroyed with it. They have to be re-declared onto the graph being
//     filled, before that body's edges resolve, and under the name the document used: an edge into
//     `continue` is name-addressed like every other edge, so a renamed one that is not recorded
//     comes back unwired, and a while loop silently reloads as a count loop.

#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/nodes/constant.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <string_view>

using lain::core::Factory;
using lain::data::Value;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	// The carry pins are DYNAMIC (addBoundary), so their type must be registered or the pin is
	// skipped on save and the loop comes back without it.
	void registerTypes()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		registerPortType<int>("Int");
		registerPortType<bool>("Bool");
	}

	ValueCodecs loopCodecs()
	{
		ValueCodecs codecs;
		codecs.registerType<int>("int");   // a loop's `count`, and a ConstantNode<int>
		codecs.registerType<bool>("bool"); // `continue`'s default, which is an ordinary param
		return codecs;
	}

	// int -> int + 1: the body of the simplest fold.
	struct AddOne : Node
	{
		PortId in, out;
		AddOne()
			: Node("AddOne")
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(in).get<int>() + 1);
		}
	};

	// int -> bool: the condition a WHILE loop wires into the reserved `continue` pin.
	struct Below : Node
	{
		PortId in, out;
		Below()
			: Node("Below")
		{
			in = addInput<int>("x");
			out = addOutput<bool>("below");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(in).get<int>() < 13);
		}
	};

	Factory<Node> makeFactory()
	{
		Factory<Node> factory;
		factory.registerType<LoopNode>("loop");
		factory.registerType<InlineGroupNode>("group");
		factory.registerType<AddOne>("addOne");
		factory.registerType<Below>("below");
		// ONE Constant kind, whatever type it emits: the type is a payload type the document
		// carries, not part of the key (ADR-0022). The preset is what a node created from this key
		// starts as, and what a document with no `types` section keeps.
		factory.registerType<ConstantNode>("constant", std::cref(portType<int>()));
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		return factory;
	}

	PortId inputNamed(const Node& node, std::string_view name)
	{
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			if (node.input(i).name() == name)
				return node.input(i).id();
		}
		return PortId{};
	}

	PortId outputNamed(const Node& node, std::string_view name)
	{
		for (std::size_t i = 0; i < node.outputCount(); ++i)
		{
			if (node.output(i).name() == name)
				return node.output(i).id();
		}
		return PortId{};
	}

	void setCount(LoopNode& loop, int bound)
	{
		const Param* param = loop.defaultOf(loop.countPort());
		REQUIRE(param != nullptr);
		REQUIRE(loop.setParam<int>(param->id(), bound));
	}

	// The one loop in a document — found the way a host does, by asking the structural question.
	NodeId onlyLoop(const Graph& graph)
	{
		for (const NodeId id : graph.nodeIds())
		{
			if (graph.node(id).interiorEvaluation() == InteriorEvaluation::PerIteration)
				return id;
		}
		return NodeId{};
	}

	// seed -> loop(carry `value`: x -> x + 1, bound `count`) -> its `value` output.
	// `condition` wires Below into the reserved `continue` pin, which is what makes it a WHILE loop.
	NodeId buildFold(Graph& graph, int seedValue, int bound, bool condition)
	{
		const NodeId id = graph.add<LoopNode>();
		auto& loop = static_cast<LoopNode&>(graph.node(id));
		setCount(loop, bound);

		const LoopNode::Carry carry = loop.addCarry<int>("value");
		REQUIRE(carry.innerIn != PortId{});

		Graph& inner = loop.inner();
		const NodeId add = inner.add<AddOne>();
		REQUIRE(inner.connect(PortAddress{inner.boundaryInputNode().id(), carry.innerIn},
							  PortAddress{add, inner.node(add).input(0).id()}) == Connection::Ok);
		REQUIRE(inner.connect(PortAddress{add, inner.node(add).output(0).id()},
							  PortAddress{inner.boundaryOutputNode().id(), carry.innerOut}) == Connection::Ok);
		if (condition)
		{
			const NodeId below = inner.add<Below>();
			REQUIRE(inner.connect(PortAddress{add, inner.node(add).output(0).id()},
								  PortAddress{below, inner.node(below).input(0).id()}) == Connection::Ok);
			REQUIRE(inner.connect(PortAddress{below, inner.node(below).output(0).id()},
								  PortAddress{inner.boundaryOutputNode().id(), loop.continuePin()}) == Connection::Ok);
		}
		edit::syncGroupPorts(graph, id);

		const NodeId seed = graph.add(constantOf(seedValue));
		REQUIRE(graph.connect(PortAddress{seed, graph.node(seed).output(0).id()},
							  PortAddress{id, inputNamed(graph.node(id), "value")}) == Connection::Ok);
		return id;
	}

	struct Folded
	{
		int value;
		int iterations;
	};

	Folded fold(const Graph& graph, NodeId loopId)
	{
		Evaluation evaluation{graph};
		SerialScheduler{}.run(graph, evaluation);
		const Node& loop = graph.node(loopId);
		return Folded{evaluation.value(PortAddress{loopId, outputNamed(loop, "value")}).get<int>(),
					  evaluation.value(PortAddress{loopId, outputNamed(loop, "iterations")}).get<int>()};
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

	// The stored node of kind `kind`, for a test that patches the document by hand.
	Value* storedNode(Value& document, std::string_view kind)
	{
		Value* nodes = mutableFind(document, "nodes");
		if (nodes == nullptr || nodes->asArray() == nullptr)
			return nullptr;
		for (Value& nodeV : *nodes->asArray())
		{
			const Value* k = nodeV.find("kind");
			if (k != nullptr && k->asString() != nullptr && *k->asString() == kind)
				return &nodeV;
		}
		return nullptr;
	}

	bool reported(const LoadResult& loaded, std::string_view fragment)
	{
		for (const LoadIssue& issue : loaded.issues)
		{
			if (issue.message.find(fragment) != std::string::npos)
				return true;
		}
		return false;
	}
} // namespace

TEST_CASE("a loop round-trips and still folds", "[flow][serialize][loop]")
{
	// Checked on VALUES, not on structure: a pairing restored onto the wrong pins, or dropped
	// altogether, still leaves a loop with exactly the right face — it just stops carrying.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	const NodeId loopId = buildFold(original, 10, 5, false);
	const Folded before = fold(original, loopId);
	REQUIRE(before.value == 15);
	REQUIRE(before.iterations == 5);

	const Value document = toValue(original, factory, codecs, {});
	LoadResult loaded = fromValue(document, factory, codecs);
	REQUIRE(loaded.issues.empty());
	REQUIRE(loaded.graph.nodeCount() == original.nodeCount());

	const NodeId reloaded = onlyLoop(loaded.graph);
	REQUIRE(reloaded != NodeId{});
	// The pairing is what came back: one carry, mirrored as a seed input and a final output of the
	// same name, beside the loop's own two ports.
	REQUIRE(static_cast<const LoopNode&>(loaded.graph.node(reloaded)).carries().size() == 1);

	const Folded after = fold(loaded.graph, reloaded);
	REQUIRE(after.value == 15); // 10 + 1 five times: the carry carried
	REQUIRE(after.iterations == 5);
}

TEST_CASE("a loop document is byte-idempotent", "[flow][serialize][loop]")
{
	// save -> load -> save must produce the identical document, or a round-trip quietly rewrites a
	// user's file — and it is the sharpest check that nothing about the loop is re-derived
	// differently on the way back in.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	buildFold(original, 10, 5, true);

	const Value first = toValue(original, factory, codecs, {});
	LoadResult loaded = fromValue(first, factory, codecs);
	const Value second = toValue(loaded.graph, factory, codecs, {});

	REQUIRE(loaded.issues.empty());
	REQUIRE(first == second);
}

TEST_CASE("a renamed reserved pin still stops the loop after a round-trip", "[flow][serialize][loop]")
{
	// THE case this slice's reserved-pin half exists for. `continue` is renameable, and the edge
	// into it is name-addressed like any other — so if the document does not record the name, the
	// loader rebuilds the pin as `continue`, the edge resolves to nothing, and the while loop comes
	// back as a count loop: no error, no empty value, just a different answer.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	const NodeId loopId = buildFold(original, 10, 100, true);
	auto& loop = static_cast<LoopNode&>(original.node(loopId));
	// Through Node::renamePort, the seam the Interface pane uses — it carries the param behind a
	// defaulted pin along with the port, which `continue` has and a bare Port::setName would strand.
	REQUIRE(loop.inner().boundaryOutputNode().renamePort(loop.continuePin(), "keepGoing"));
	REQUIRE(loop.inner().boundaryInputNode().renamePort(loop.indexPin(), "i"));

	// It converges long before the bound of 100: Below stops it once the value reaches 13.
	const Folded before = fold(original, loopId);
	REQUIRE(before.value == 13);
	REQUIRE(before.iterations == 3);

	const Value document = toValue(original, factory, codecs, {});
	LoadResult loaded = fromValue(document, factory, codecs);

	// The VALUE first, because it is the claim. Without the stored names the pin is rebuilt as
	// `continue`, the edge into it resolves to nothing, the now-unwired condition falls back to its
	// default of true, and the loop runs to its bound instead of converging. The lost edge IS
	// reported, so this is not perfectly silent — but a warning beside a different answer is still a
	// different answer, and the graph runs either way.
	const NodeId reloaded = onlyLoop(loaded.graph);
	const Folded after = fold(loaded.graph, reloaded);
	REQUIRE(after.value == 13);		// not 110, which is what the bound alone would give
	REQUIRE(after.iterations == 3); // not 100

	const auto& reloadedLoop = static_cast<const LoopNode&>(loaded.graph.node(reloaded));
	REQUIRE(reloadedLoop.inner().boundaryOutputNode().input(reloadedLoop.continuePin()).name() == "keepGoing");
	REQUIRE(reloadedLoop.inner().boundaryInputNode().output(reloadedLoop.indexPin()).name() == "i");
	REQUIRE(loaded.issues.empty());
}

TEST_CASE("the interior comes back with its reserved pins, exactly once", "[flow][serialize][loop]")
{
	// The pins are static, so they are neither written nor replayed: they exist on the reloaded
	// interior only because the loader declares them onto the graph it is filling. Declared FIRST,
	// so the document's own dynamic pins land after them and nothing is added twice.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	buildFold(original, 10, 5, true);
	LoadResult loaded = fromValue(toValue(original, factory, codecs, {}), factory, codecs);

	const auto& loop = static_cast<const LoopNode&>(loaded.graph.node(onlyLoop(loaded.graph)));
	const GroupInputNode& into = loop.inner().boundaryInputNode();
	const GroupOutputNode& from = loop.inner().boundaryOutputNode();

	// `index` + the carry pin; `continue` + the carry pin. One of each, not two.
	REQUIRE(into.outputCount() == 2);
	REQUIRE(from.inputCount() == 2);
	REQUIRE(into.findOutput(loop.indexPin()) != nullptr);
	REQUIRE(from.findInput(loop.continuePin()) != nullptr);
	REQUIRE_FALSE(into.output(loop.indexPin()).isDynamic());
	REQUIRE_FALSE(from.input(loop.continuePin()).isDynamic());

	// And neither reaches the outer face: `count` / `iterations` plus the carry's two, no more.
	REQUIRE(loop.inputCount() == 2);
	REQUIRE(loop.outputCount() == 2);
	REQUIRE(outputNamed(loop, "index") == PortId{});
	REQUIRE(inputNamed(loop, "continue") == PortId{});
}

TEST_CASE("a carry naming a pin that has vanished is reported, not silently kept", "[flow][serialize][loop]")
{
	// Rectification, the same courtesy a map's stored interface and a linked group's cache get: the
	// document claims a pairing the rebuilt interior cannot support, so it is dropped and said out
	// loud. The cost is that carry alone — the loop still loads, and still runs.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	const NodeId loopId = buildFold(original, 10, 5, false);
	Value document = toValue(original, factory, codecs, {});

	Value* loopNode = storedNode(document, "loop");
	REQUIRE(loopNode != nullptr);
	Value* section = mutableFind(*loopNode, "loop");
	REQUIRE(section != nullptr);
	Value* carries = mutableFind(*section, "carries");
	REQUIRE(carries != nullptr);
	Value ghost = Value::object();
	ghost.set("in", Value(std::string{"ghost"}));
	ghost.set("out", Value(std::string{"ghost"}));
	carries->push(std::move(ghost));

	LoadResult loaded = fromValue(document, factory, codecs);
	REQUIRE(reported(loaded, "ghost"));

	// The real carry is untouched, so the fold is unchanged.
	const NodeId reloaded = onlyLoop(loaded.graph);
	REQUIRE(static_cast<const LoopNode&>(loaded.graph.node(reloaded)).carries().size() == 1);
	REQUIRE(fold(loaded.graph, reloaded).value == 15);
	(void)loopId;
}

TEST_CASE("a loop with no stored section loads with no carries", "[flow][serialize][loop]")
{
	// Forward compatibility with a hand-written document, and the fallback if the section is missing
	// for any reason. A loop with no carry is legal, just degenerate: the pin becomes an invariant
	// input and a last-iteration output, so the body runs N times on the same seed and the answer is
	// one step rather than five. No issue is raised — nothing went wrong, the document said nothing.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	buildFold(original, 10, 5, false);
	Value document = toValue(original, factory, codecs, {});
	Value* loopNode = storedNode(document, "loop");
	REQUIRE(loopNode != nullptr);
	eraseKey(*loopNode, "loop");

	LoadResult loaded = fromValue(document, factory, codecs);
	REQUIRE(loaded.issues.empty());

	const NodeId reloaded = onlyLoop(loaded.graph);
	const auto& loop = static_cast<const LoopNode&>(loaded.graph.node(reloaded));
	REQUIRE(loop.carries().empty());
	REQUIRE(loop.inner().boundaryInputNode().output(loop.indexPin()).name() == "index");

	const Folded after = fold(loaded.graph, reloaded);
	REQUIRE(after.value == 11); // the seed, stepped once — every iteration started over
	REQUIRE(after.iterations == 5);
}

TEST_CASE("a loop's bound and its condition's default round-trip as params", "[flow][serialize][loop]")
{
	// `count` is a defaulted input, and a default IS a param underneath — so it needs nothing of its
	// own here. `continue`'s Default{true} is the same, on the inner GroupOutputNode: a param the
	// loader can only read back because the reserved pin (and therefore its param) has been declared
	// by the time this node's params are read.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph original;
	const NodeId loopId = buildFold(original, 10, 7, false);
	auto& loop = static_cast<LoopNode&>(original.node(loopId));

	// Turn the condition off at its default: unwired, so the default decides, and it says stop.
	GroupOutputNode& from = loop.inner().boundaryOutputNode();
	const Param* fallback = from.defaultOf(loop.continuePin());
	REQUIRE(fallback != nullptr);
	REQUIRE(from.setParam<bool>(fallback->id(), false));

	const Folded before = fold(original, loopId);
	REQUIRE(before.iterations == 1); // a condition false at once still runs one iteration
	REQUIRE(before.value == 11);

	LoadResult loaded = fromValue(toValue(original, factory, codecs, {}), factory, codecs);
	REQUIRE(loaded.issues.empty());

	const NodeId reloaded = onlyLoop(loaded.graph);
	const auto& reloadedLoop = static_cast<const LoopNode&>(loaded.graph.node(reloaded));
	REQUIRE(reloadedLoop.param(reloadedLoop.defaultOf(reloadedLoop.countPort())->id()).get<int>() == 7);

	const Folded after = fold(loaded.graph, reloaded);
	REQUIRE(after.iterations == 1);
	REQUIRE(after.value == 11);
}

TEST_CASE("a loop nested inside an inline group round-trips", "[flow][serialize][loop]")
{
	// The hook that declares the reserved pins is handed to the LOOP's own body and to nothing else,
	// so it must not be inherited down the recursion — an inline group's boundary nodes would then
	// grow an `index` pin of their own.
	registerTypes();
	const Factory<Node> factory = makeFactory();
	const ValueCodecs codecs = loopCodecs();

	Graph parent;
	const NodeId groupId = parent.add<InlineGroupNode>();
	auto& group = static_cast<InlineGroupNode&>(parent.node(groupId));
	const NodeId loopId = buildFold(group.inner(), 10, 4, false);

	const PortId resultPin = group.inner().boundaryOutputNode().addBoundary<int>("result");
	REQUIRE(group.inner().connect(PortAddress{loopId, outputNamed(group.inner().node(loopId), "value")},
								  PortAddress{group.inner().boundaryOutputNode().id(), resultPin}) == Connection::Ok);
	edit::syncGroupPorts(parent, groupId);

	const Value first = toValue(parent, factory, codecs, {});
	LoadResult loaded = fromValue(first, factory, codecs);
	REQUIRE(loaded.issues.empty());
	REQUIRE(toValue(loaded.graph, factory, codecs, {}) == first);

	NodeId reloadedGroup;
	for (const NodeId id : loaded.graph.nodeIds())
	{
		if (loaded.graph.node(id).interiorEvaluation() == InteriorEvaluation::Once && loaded.graph.node(id).innerGraph() != nullptr)
			reloadedGroup = id;
	}
	REQUIRE(reloadedGroup != NodeId{});

	// The group's OWN boundary is untouched by the loop's reserved pins: one pin, the result.
	const Graph& body = *loaded.graph.node(reloadedGroup).innerGraph();
	REQUIRE(body.boundaryInputNode().outputCount() == 0);
	REQUIRE(body.boundaryOutputNode().inputCount() == 1);

	Evaluation evaluation{loaded.graph};
	SerialScheduler{}.run(loaded.graph, evaluation);
	REQUIRE(evaluation.value(PortAddress{reloadedGroup, outputNamed(loaded.graph.node(reloadedGroup), "result")}).get<int>() == 14);
}
