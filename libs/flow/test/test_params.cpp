// Unit: node parameters — typed, non-connectable configuration slots declared with addParam<T>,
// read in compute() through the PortId the declaration returned, and edited by the adapter while
// iterating by position. Pure CPU, no device.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/portvalue.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

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

	void compute(NodeEvaluation& evaluation) const override
	{
		evaluation.output(m_out).set<int>(param(m_count).get<int>()); // read a param like a real node
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
	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, id);
	REQUIRE(e.value(PortAddress{id, graph.node(id).output(0).id()}).get<int>() == 3);
}

TEST_CASE("editing a param changes what compute reads", "[param]")
{
	Graph graph;
	const NodeId id = graph.add<ConfigNode>();

	Node& node = graph.node(id);
	REQUIRE(node.setParam(node.param(0).id(), 7)); // the adapter edits through the one seam
	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, id);
	REQUIRE(e.value(PortAddress{id, node.output(0).id()}).get<int>() == 7);
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
	Evaluation e{graph};
	const PortAddress out{id, node.output(0).id()};

	SerialScheduler{}.evaluate(graph, e, id);
	REQUIRE(e.value(out).get<int>() == 3);
	REQUIRE_FALSE(e.needsRecompute(id)); // the run recorded it at the definition's version

	REQUIRE(node.setParam(node.param(0).id(), 12));
	REQUIRE(e.needsRecompute(id)); // ... and the edit bumped that version, with no separate call

	SerialScheduler{}.evaluate(graph, e, id);
	REQUIRE(e.value(out).get<int>() == 12);

	// A REFUSED edit must not bump the version either — it changed nothing, so there is nothing to
	// recompute, and a spurious bump would silently cost every evaluation a re-run of the cone.
	const std::uint64_t version = node.version();
	REQUIRE_FALSE(node.setParam(node.param(0).id(), 1.5f));
	REQUIRE(node.version() == version);
	REQUIRE_FALSE(e.needsRecompute(id));
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

namespace
{
	// One input, declared WITH A DEFAULT — the shape a node uses for a setting that may be either
	// configured on the node or driven by the graph.
	struct Defaulted : Node
	{
		PortId in, out;
		explicit Defaulted(int fallback = 7)
			: Node("Defaulted")
		{
			in = addInput<int>("value", Default{fallback});
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			// Read as an ordinary input: unconnected, the slot carries the default, so there is
			// nothing to check for here.
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// Produces nothing at all — a Gate turned off, in miniature.
	struct Suppressor : Node
	{
		PortId out;
		Suppressor()
			: Node("Suppressor")
		{
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).clear(); }
	};
} // namespace

TEST_CASE("an unconnected input carries its declared default", "[param][default]")
{
	Graph graph;
	const NodeId id = graph.add<Defaulted>(7);
	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	REQUIRE(test::output(graph, evaluation, id, 0).get<int>() == 7);

	SECTION("and the default is a param, so editing it re-runs the node")
	{
		// Which is the point of it being a Param and not a bare member: it serializes, the inspector
		// edits it through the ordinary setParam seam, and that seam invalidates.
		Node& node = graph.node(id);
		const Param* fallback = node.defaultOf(node.input(0).id());
		REQUIRE(fallback != nullptr);
		REQUIRE(node.setParam<int>(fallback->id(), 12));

		SerialScheduler{}.run(graph, evaluation);
		REQUIRE(test::output(graph, evaluation, id, 0).get<int>() == 12);
	}

	SECTION("an ordinary input has no default")
	{
		Graph plain;
		const NodeId sink = plain.add<test::AddInt>();
		REQUIRE(plain.node(sink).defaultOf(plain.node(sink).input(0).id()) == nullptr);
	}
}

TEST_CASE("a wired value overrides the default", "[param][default]")
{
	Graph graph;
	const NodeId source = graph.add<test::ConstInt>(99);
	const NodeId id = graph.add<Defaulted>(7);
	REQUIRE(graph.connect(source, 0, id, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	REQUIRE(test::output(graph, evaluation, id, 0).get<int>() == 99);
}

TEST_CASE("a default never stands in for a suppressed upstream", "[param][default]")
{
	// THE safety rule, and the reason a defaulted input stays REQUIRED rather than Optional. If the
	// default filled any empty slot, then wiring a gate that is off would feed the node its default
	// instead of suppressing it — a default quietly undoing conditional evaluation (ADR-0007).
	//
	// So the default seeds an input with NO INCOMING EDGE. Connected and producing nothing means
	// exactly that: no value, the node is not ready, and the suppression propagates.
	Graph graph;
	const NodeId gate = graph.add<Suppressor>();
	const NodeId id = graph.add<Defaulted>(7);
	REQUIRE(graph.connect(gate, 0, id, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	REQUIRE_FALSE(evaluation.ready(id));											  // not ready: its required input is empty
	REQUIRE(test::output(graph, evaluation, id, 0).empty());						  // so it produced nothing...
	REQUIRE(evaluation.value(PortAddress{id, graph.node(id).input(0).id()}).empty()); // ...and was NOT defaulted
}

TEST_CASE("renaming a defaulted port carries its param with it", "[param][default]")
{
	// A rename is display-only for the port — edges reference the PortId — but a DEFAULTED input has
	// a param behind it carrying the same name, and a param is addressed BY NAME on disk. Renaming
	// only the port leaves a saved document naming a param the reloaded node does not declare, so
	// the default silently reverts to the constructor's. Node::renamePort moves both, which is why
	// it exists rather than callers reaching for Port::setName.
	Defaulted node{7};
	const Param* fallback = node.defaultOf(node.in);
	REQUIRE(fallback != nullptr);
	REQUIRE(fallback->name() == "value");

	REQUIRE(node.renamePort(node.in, "seed"));
	REQUIRE(node.input(node.in).name() == "seed");
	// The SAME param — identity is the PortId, so only the label moved.
	REQUIRE(node.defaultOf(node.in) == fallback);
	REQUIRE(fallback->name() == "seed");
	REQUIRE(fallback->get<int>() == 7);
}

TEST_CASE("renamePort refuses a name that would make an edge ambiguous", "[param][default]")
{
	// Port names are the on-disk edge key, so two ports sharing one on a side makes every edge
	// through them ambiguous on load. A primitive reports and changes nothing; it does not decide.
	struct TwoInputs : Node
	{
		PortId a, b;
		TwoInputs()
			: Node("TwoInputs")
		{
			a = addInput<int>("a", Default{1});
			b = addInput<int>("b");
		}
		void compute(NodeEvaluation&) const override {}
	};

	TwoInputs node;
	REQUIRE_FALSE(node.renamePort(node.a, "b"));	 // already taken on this side
	REQUIRE_FALSE(node.renamePort(node.a, "2fast")); // not a valid port name
	REQUIRE_FALSE(node.renamePort(PortId{}, "c"));	 // no such port
	REQUIRE(node.input(node.a).name() == "a");
	REQUIRE(node.defaultOf(node.a)->name() == "a");

	// The port's OWN name is not a collision with itself.
	REQUIRE(node.renamePort(node.a, "a"));
}
