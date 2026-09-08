// PAYLOAD TYPES (ADR-0022): a node's type as data — the mechanism, the retype gesture, and the
// value-conversion registry it carries a param's value across with.
//
// The engine-side proof. What a HOST does with it (the Inspector's dropdown, the generated palette
// entries) is exercised in apps/flowview/test.

#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/nodes/cast.h>
#include <lain/flow/nodes/constant.h>
#include <lain/flow/nodes/gate.h>
#include <lain/flow/nodes/merge.h>
#include <lain/flow/nodes/select.h>
#include <lain/flow/porttype.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace lain::flow;

namespace
{
	// A node with ONE payload type and an UNTAGGED port beside it — the shape a Gate has, and the
	// only shape that can show a retype leaving an edge alone.
	class PassThroughNode : public Node
	{
	public:
		explicit PassThroughNode(const PortType& type)
			: Node("PassThrough")
		{
			addPayloadType("value", type);
			m_enable = addInput<bool>("enable", Default{true}); // untagged: never retyped
			m_in = addInputOf("value", "in");
			m_out = addOutputOf("value", "out");
		}

		void compute(NodeEvaluation& evaluation) const override
		{
			if (evaluation.input(m_enable).get<bool>())
				evaluation.output(m_out) = evaluation.input(m_in);
		}

		PortId enablePort() const { return m_enable; }
		PortId inPort() const { return m_in; }
		PortId outPort() const { return m_out; }

	private:
		PortId m_enable;
		PortId m_in;
		PortId m_out;
	};

	// No operator<, so PortType::compare is null and OrderedOnlyNode refuses it.
	struct Unordered
	{
		int x = 0;
	};

	// Refuses anything that is not ORDERABLE — the shape CompareNode takes in slice 3, here to prove
	// acceptsPayloadType actually gates both the primitive and the gesture.
	class OrderedOnlyNode : public Node
	{
	public:
		OrderedOnlyNode()
			: Node("OrderedOnly")
		{
			addPayloadType("value", portType<int>());
			m_in = addInputOf("value", "in");
		}

		bool acceptsPayloadType(const std::string&, const PortType& type) const override
		{
			return type.isOrderable();
		}

		void compute(NodeEvaluation&) const override {}

	private:
		PortId m_in;
	};

	// A sink of a fixed type, so an edge into it can be made and then broken by a retype.
	template <typename T>
	class SinkNode : public Node
	{
	public:
		SinkNode()
			: Node("Sink")
		{
			m_in = addInput<T>("in");
		}

		void compute(NodeEvaluation&) const override {}
		PortId inPort() const { return m_in; }

	private:
		PortId m_in;
	};

	// The conversions these cases lean on. Registered by the test, because flow core registers none:
	// which conversions exist is the app's decision (ADR-0022), and this file is standing in for one.
	void registerTestConversions()
	{
		registerConversion<int, float>();
		registerConversion<int, std::string>(+[](const int& v) -> std::string
											 { return std::to_string(v); });
	}
} // namespace

TEST_CASE("a node's payload type is what its tagged declarations are declared from", "[payload]")
{
	ConstantNode constant{portType<int>()};

	REQUIRE(constant.payloadTypes().size() == 1);
	CHECK(constant.payloadTypes()[0].name == "value");
	CHECK(constant.payloadTypes()[0].type == &portType<int>());
	CHECK(constant.payloadType("value") == &portType<int>());
	CHECK(constant.payloadType("nope") == nullptr);

	// Both the output port and the value param were declared FROM it, so both carry its type.
	CHECK(constant.output(0).type() == typeid(int));
	CHECK(constant.param(0).type() == typeid(int));
	CHECK(constant.declarationsOf("value").size() == 2);
	CHECK(constant.declarationsOf("value2").empty());

	// A node with none answers empty rather than needing to be asked whether it has any.
	SinkNode<int> plain;
	CHECK(plain.payloadTypes().empty());
	CHECK(plain.declarationsOf("value").empty());
}

TEST_CASE("retyping moves every tagged declaration and leaves the rest alone", "[payload]")
{
	Graph graph;
	const NodeId id = graph.add<PassThroughNode>(portType<int>());
	const auto& node = static_cast<const PassThroughNode&>(graph.node(id));

	REQUIRE(node.input(node.inPort()).type() == typeid(int));
	const PortId inId = node.inPort();
	const PortId outId = node.outPort();
	const std::uint64_t before = node.version();

	REQUIRE(graph.setPayloadType(id, "value", portType<float>()));

	CHECK(node.input(inId).type() == typeid(float));
	CHECK(node.output(outId).type() == typeid(float));
	// IN PLACE: the ids, names and presence survive, which is what an edge and a canvas both rely on.
	CHECK(node.input(inId).id() == inId);
	CHECK(node.input(inId).name() == "in");
	CHECK(node.output(outId).name() == "out");
	// The untagged `enable` is untouched — a retype moves what was declared from the payload type.
	CHECK(node.input(node.enablePort()).type() == typeid(bool));
	// The recipe changed, so every evaluation of it is stale.
	CHECK(node.version() > before);
}

TEST_CASE("a payload type a node does not declare, or will not accept, changes nothing", "[payload]")
{
	Graph graph;
	const NodeId id = graph.add<OrderedOnlyNode>();
	const Node& node = graph.node(id);
	const std::uint64_t before = node.version();

	// No payload type by that name.
	CHECK_FALSE(graph.setPayloadType(id, "other", portType<float>()));
	// Declared, but REFUSED: this node needs an ordering, and Unordered has no operator<, so its
	// PortType::compare is null. The node asks the capability rather than listing types.
	CHECK_FALSE(graph.setPayloadType(id, "value", portType<Unordered>()));
	// Retyping to the type it already has succeeds and bumps NOTHING — nothing is declared
	// differently, so charging every evaluation a re-run would be a lie.
	CHECK(graph.setPayloadType(id, "value", portType<int>()));
	CHECK(node.version() == before);
	CHECK(node.input(0).type() == typeid(int));

	// And an accepted one goes through.
	CHECK(graph.setPayloadType(id, "value", portType<float>()));
	CHECK(node.input(0).type() == typeid(float));
	CHECK(node.version() > before);
}

TEST_CASE("the retype primitive refuses while an edge would be left with ends that disagree", "[payload]")
{
	// THE hazard the whole split exists for: connect() type-checks once and nothing re-checks —
	// Scheduler::populateInputs copies into the slot blind — so a retype under a live edge would
	// install a wrongly-typed payload and throw inside a worker task.
	Graph graph;
	const NodeId source = graph.add(constantOf(7));
	const NodeId sink = graph.add<SinkNode<int>>();
	REQUIRE(graph.connect(source, 0, sink, 0) == Connection::Ok);

	CHECK_FALSE(graph.setPayloadType(source, "value", portType<float>()));
	// Nothing moved: the refusal is atomic.
	CHECK(graph.node(source).output(0).type() == typeid(int));
	CHECK(graph.edges().size() == 1);
}

TEST_CASE("the gesture cuts the edges the primitive would refuse for, and reports them", "[payload]")
{
	Graph graph;
	const NodeId source = graph.add(constantOf(7));
	const NodeId gate = graph.add<PassThroughNode>(portType<int>());
	const NodeId enable = graph.add(constantOf(true));
	const auto& node = static_cast<const PassThroughNode&>(graph.node(gate));

	const PortAddress sourceOut{source, graph.node(source).output(0).id()};
	REQUIRE(graph.connect(sourceOut, PortAddress{gate, node.inPort()}) == Connection::Ok);
	REQUIRE(graph.connect(PortAddress{enable, graph.node(enable).output(0).id()},
						  PortAddress{gate, node.enablePort()}) == Connection::Ok);
	REQUIRE(graph.edges().size() == 2);

	const edit::RetypeResult result = edit::setPayloadType(graph, gate, "value", &portType<float>());

	REQUIRE(result.ok);
	CHECK(node.input(node.inPort()).type() == typeid(float));
	// The edge into the retyped pin is gone and NAMED; the one into the untagged `enable` stands.
	REQUIRE(result.disconnected.size() == 1);
	CHECK(result.disconnected[0] == PortAddress{gate, node.inPort()});
	REQUIRE(graph.edges().size() == 1);
	CHECK(graph.edges()[0].to == PortAddress{gate, node.enablePort()});
}

TEST_CASE("a retype carries a param's value across where a conversion exists", "[payload]")
{
	registerTestConversions();

	Graph graph;
	const NodeId id = graph.add(constantOf(5));
	const auto& constant = static_cast<const ConstantNode&>(graph.node(id));
	REQUIRE(constant.value<int>() == 5);

	// int -> float is registered, so the number the user typed survives the retype.
	edit::RetypeResult toFloat = edit::setPayloadType(graph, id, "value", &portType<float>());
	REQUIRE(toFloat.ok);
	CHECK(toFloat.reset.empty());
	CHECK(constant.value<float>() == 5.0f);

	// float -> path is NOT registered, so the value cannot come across. It becomes the new type's
	// default and is REPORTED by name — the whole point of reporting, since a number silently
	// becoming 0 is the failure this avoids.
	edit::RetypeResult toPath = edit::setPayloadType(graph, id, "value", &portType<std::filesystem::path>());
	REQUIRE(toPath.ok);
	REQUIRE(toPath.reset.size() == 1);
	CHECK(toPath.reset[0] == "value");
	CHECK(constant.value<std::filesystem::path>() == std::filesystem::path{});
}

TEST_CASE("a retyped constant emits its new type through the scheduler", "[payload]")
{
	// The end-to-end check: the declarations, the param and the produced VALUE all move together,
	// so a downstream node reads what the payload type says it will.
	registerTestConversions();

	Graph graph;
	const NodeId id = graph.add(constantOf(3));
	Evaluation evaluation{graph};
	SerialScheduler scheduler;

	scheduler.run(graph, evaluation);
	const PortAddress out{id, graph.node(id).output(0).id()};
	REQUIRE(evaluation.value(out).holds<int>());
	CHECK(evaluation.value(out).get<int>() == 3);

	REQUIRE(edit::setPayloadType(graph, id, "value", &portType<float>()).ok);
	scheduler.run(graph, evaluation);
	REQUIRE(evaluation.value(out).holds<float>());
	CHECK(evaluation.value(out).get<float>() == 3.0f);
}

TEST_CASE("a value conversion answers a value, an empty slot, or nothing at all", "[payload][conversion]")
{
	registerTestConversions();

	PortValue five;
	five.set<int>(5);

	CHECK(conversionRegistered(typeid(int), typeid(float)));
	// A type is not convertible to ITSELF: nothing needs converting, and registering one would put
	// an identity Cast in every menu.
	CHECK_FALSE(conversionRegistered(typeid(int), typeid(int)));
	CHECK_FALSE(conversionRegistered(typeid(float), typeid(std::filesystem::path)));

	CHECK(convertValue(five, portType<float>()).get<float>() == 5.0f);
	CHECK(convertValue(five, portType<std::string>()).get<std::string>() == "5");
	// Asking for the type it already is hands the value straight back, rather than needing a
	// registration for every type against itself.
	CHECK(convertValue(five, portType<int>()).get<int>() == 5);
	// No conversion, and an empty input, are the same answer: nothing was produced (ADR-0007).
	CHECK(convertValue(five, portType<std::filesystem::path>()).empty());
	CHECK(convertValue(PortValue{}, portType<float>()).empty());
}

TEST_CASE("a fallible conversion answers an empty value rather than a wrong one", "[payload][conversion]")
{
	// What a Cast from text does with a typo: it produces nothing and suppresses, instead of reading
	// 12 out of "12x". The strict-parse policy clibinders.cpp already applies to a cli value.
	registerConversion<std::string, int>(+[](const std::string& text) -> std::optional<int>
										 {
											 try
											 {
												 std::size_t used = 0;
												 const int value = std::stoi(text, &used);
												 return used == text.size() ? std::optional<int>{value} : std::nullopt;
											 }
											 catch (const std::exception&)
											 {
												 return std::nullopt;
											 }
										 });

	PortValue text;
	text.set<std::string>("12");
	CHECK(convertValue(text, portType<int>()).get<int>() == 12);

	text.set<std::string>("12x");
	CHECK(convertValue(text, portType<int>()).empty());
	text.set<std::string>("hello");
	CHECK(convertValue(text, portType<int>()).empty());
}

TEST_CASE("the ordering capability is filled for exactly the types that can be ordered", "[payload][porttype]")
{
	// Derived from the type, not listed — which is what lets a Compare's accepted set grow with the
	// registered types instead of rotting (ADR-0022).
	CHECK(portType<int>().isOrderable());
	CHECK(portType<float>().isOrderable());
	CHECK(portType<std::string>().isOrderable());
	CHECK(portType<std::filesystem::path>().isOrderable());

	// A std::vector is asked about its ELEMENT. Before C++20 its operator< is declared for every
	// element type and fails only when instantiated, so plain detection would answer true here and
	// hard-error on the first real comparison.
	CHECK(portType<std::vector<int>>().isOrderable());
	CHECK_FALSE(portType<std::vector<Unordered>>().isOrderable());
	CHECK_FALSE(portType<Unordered>().isOrderable());
	// And it recurses, since the container arm defers to the element's own answer.
	CHECK(portType<std::vector<std::vector<int>>>().isOrderable());
	CHECK_FALSE(portType<std::vector<std::vector<Unordered>>>().isOrderable());

	const PortType& ints = portType<int>();
	PortValue two;
	two.set<int>(2);
	PortValue nine;
	nine.set<int>(9);
	CHECK(ints.compare(two, nine) == -1);
	CHECK(ints.compare(nine, two) == 1);
	CHECK(ints.compare(two, two) == 0);
	// An empty slot has no answer; 0 keeps it a total function, as `at` and `size` are.
	CHECK(ints.compare(two, PortValue{}) == 0);
}

TEST_CASE("a type's default value is what a retyped param falls back to", "[payload][porttype]")
{
	REQUIRE(portType<int>().defaultValue != nullptr);
	CHECK(portType<int>().defaultValue().get<int>() == 0);
	CHECK(portType<std::string>().defaultValue().get<std::string>().empty());
	CHECK(portType<std::vector<int>>().defaultValue().get<std::vector<int>>().empty());
}

TEST_CASE("a cast converts through the registry, and produces nothing when it cannot", "[payload][cast]")
{
	registerTestConversions();

	Graph graph;
	const NodeId source = graph.add(constantOf(7));
	const NodeId cast = graph.add<CastNode>(portType<int>(), portType<float>());
	REQUIRE(graph.connect(PortAddress{source, graph.node(source).output(0).id()},
						  PortAddress{cast, graph.node(cast).input(0).id()}) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler scheduler;
	scheduler.run(graph, evaluation);

	const PortAddress out{cast, graph.node(cast).output(0).id()};
	REQUIRE(evaluation.value(out).holds<float>());
	CHECK(evaluation.value(out).get<float>() == 7.0f);
	CHECK(static_cast<const CastNode&>(graph.node(cast)).canConvert());

	// Retype the target to something nothing converts an int to. The pair is REPRESENTABLE — the
	// alternative would make loading order-dependent — so it produces nothing and says so.
	REQUIRE(edit::setPayloadType(graph, cast, CastNode::kToPayload, &portType<std::filesystem::path>()).ok);
	CHECK_FALSE(static_cast<const CastNode&>(graph.node(cast)).canConvert());

	scheduler.run(graph, evaluation);
	CHECK(evaluation.value(PortAddress{cast, graph.node(cast).output(0).id()}).empty());
}

TEST_CASE("a cast of a type to itself is a pass-through, not a broken node", "[payload][cast]")
{
	// Nothing registers a conversion from a type to ITSELF (registerConversion refuses one), so a
	// Cast whose ends match has to be answered by convertValue rather than by a registration —
	// otherwise the state a user passes THROUGH while changing both ends reads as broken.
	registerTestConversions();

	Graph graph;
	const NodeId source = graph.add(constantOf(4));
	const NodeId cast = graph.add<CastNode>(portType<int>(), portType<int>());
	REQUIRE(graph.connect(PortAddress{source, graph.node(source).output(0).id()},
						  PortAddress{cast, graph.node(cast).input(0).id()}) == Connection::Ok);

	CHECK(static_cast<const CastNode&>(graph.node(cast)).canConvert());

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);
	CHECK(evaluation.value(PortAddress{cast, graph.node(cast).output(0).id()}).get<int>() == 4);
}

TEST_CASE("retyping a cast's input keeps the edge only when the source already matches", "[payload][cast]")
{
	// The retype contract seen through the node it exists for: the edge into `in` is cut, because
	// its source is an int and `in` is about to stop being one.
	registerTestConversions();

	Graph graph;
	const NodeId source = graph.add(constantOf(7));
	const NodeId cast = graph.add<CastNode>(portType<int>(), portType<float>());
	REQUIRE(graph.connect(PortAddress{source, graph.node(source).output(0).id()},
						  PortAddress{cast, graph.node(cast).input(0).id()}) == Connection::Ok);

	const edit::RetypeResult result = edit::setPayloadType(graph, cast, CastNode::kFromPayload, &portType<float>());
	REQUIRE(result.ok);
	CHECK(result.disconnected.size() == 1);
	CHECK(graph.edges().empty());
	CHECK(graph.node(cast).input(0).type() == typeid(float));
	// The OTHER payload type is untouched: they are separate facts about the node.
	CHECK(graph.node(cast).output(0).type() == typeid(float));
}

TEST_CASE("retyping a merge moves its DYNAMIC branches, not just its declared output", "[payload][dynamic]")
{
	// The case Merge and Select are in scope FOR. A branch is added at runtime through the port-type
	// registry, whose creator has a compile-time T and knows nothing about payload types — so
	// without the tag a retype would move the constructor-declared `out` and leave every branch
	// behind, and the node would forward nothing while looking correctly configured.
	registerPortType<int>("Int");
	registerPortType<float>("Float");

	Graph graph;
	const NodeId merge = graph.add<MergeNode>(portType<int>());
	const NodeId source = graph.add(constantOf(7));

	// Through the production gesture, which routes via the registry exactly as the canvas "+" does.
	const PortId a = edit::addPort(graph, merge, "Int", "a");
	const PortId b = edit::addPort(graph, merge, "Int", "b");
	REQUIRE(a != PortId{});
	REQUIRE(b != PortId{});
	REQUIRE(graph.connect(PortAddress{source, graph.node(source).output(0).id()}, PortAddress{merge, a}) == Connection::Ok);

	// Homogeneous: the "+" menu offers only the payload type.
	CHECK(static_cast<const MergeNode&>(graph.node(merge)).acceptsPortType("Int"));
	CHECK_FALSE(static_cast<const MergeNode&>(graph.node(merge)).acceptsPortType("Float"));

	const edit::RetypeResult result = edit::setPayloadType(graph, merge, MergeNode::kValuePayload, &portType<float>());
	REQUIRE(result.ok);

	const Node& node = graph.node(merge);
	CHECK(node.output(0).type() == typeid(float)); // the declared output
	CHECK(node.input(a).type() == typeid(float));  // and BOTH runtime branches
	CHECK(node.input(b).type() == typeid(float));
	// The branches keep their ids and their Optional presence — a retype moves the type, nothing else.
	CHECK(node.input(a).id() == a);
	CHECK_FALSE(node.input(a).required());
	CHECK(node.input(a).isDynamic());

	// The int source no longer fits, so its edge was cut and named.
	REQUIRE(result.disconnected.size() == 1);
	CHECK(result.disconnected[0] == PortAddress{merge, a});
	CHECK(graph.edges().empty());

	// And the menu follows the node.
	CHECK(static_cast<const MergeNode&>(node).acceptsPortType("Float"));
	CHECK_FALSE(static_cast<const MergeNode&>(node).acceptsPortType("Int"));
}

TEST_CASE("a retype leaves a control node's non-payload pins alone", "[payload][dynamic]")
{
	// A Gate is switched by a bool and a Select is indexed by an int, whatever they carry — so
	// neither pin is declared from the payload type and neither moves.
	Graph graph;
	const NodeId gate = graph.add<GateNode>(portType<int>());
	const NodeId select = graph.add<SelectNode>(portType<int>());

	REQUIRE(graph.setPayloadType(gate, GateNode::kValuePayload, portType<std::string>()));
	REQUIRE(graph.setPayloadType(select, SelectNode::kValuePayload, portType<std::string>()));

	CHECK(graph.node(gate).input(0).type() == typeid(bool)); // enable
	CHECK(graph.node(gate).input(1).type() == typeid(std::string));
	CHECK(graph.node(gate).output(0).type() == typeid(std::string));
	CHECK(graph.node(select).input(0).type() == typeid(int)); // selector
	CHECK(graph.node(select).output(0).type() == typeid(std::string));
}
