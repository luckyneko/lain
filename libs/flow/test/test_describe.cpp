// Unit tests for Evaluation::describe() — the flow side of the value-display pathway: the PortType
// flyweight's type-erasure bridge over lain::meta::toString, plus the empty slot. The ladder itself
// is covered by (meta) test_tostring; here we confirm an evaluation value renders through it.
// (It used to be Port::describe; the value moved to the Evaluation, and rendering followed it while
// staying a property of the declared type.) GPU-free.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lain::flow;

namespace lain::flow::test
{
	struct Stamped
	{
		std::string toString() const { return "stamped!"; }
	};

	struct Opaque
	{
		int x;
	}; // no toString / ostream -> type-name fallback

	// One output per display path: ostream scalar, bool, string, member toString, opaque.
	struct DescribeNode : Node
	{
		PortId i, b, s, st, op;
		DescribeNode()
			: Node("Describe")
		{
			i = addOutput<int>("i");
			b = addOutput<bool>("b");
			s = addOutput<std::string>("s");
			st = addOutput<Stamped>("st");
			op = addOutput<Opaque>("op");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(i).set(42);
			evaluation.output(b).set(true);
			evaluation.output(s).set(std::string("hi"));
			evaluation.output(st).set(Stamped{});
			evaluation.output(op).set(Opaque{7});
		}
	};
} // namespace lain::flow::test

using namespace lain::flow::test;

TEST_CASE("describe renders an unset port as empty", "[describe]")
{
	Graph g;
	const NodeId n = g.add<DescribeNode>();
	Evaluation e{g};
	REQUIRE(e.describe(PortAddress{n, g.node(n).output(0).id()}) == "(empty)"); // nothing has run
}

TEST_CASE("describe bridges a type-erased value through meta::toString", "[describe]")
{
	// Rendering follows the VALUE into the Evaluation, but stays the declared TYPE's capability
	// (PortType::describe) — so a type still describes itself with no central ladder.
	Graph g;
	const NodeId n = g.add<DescribeNode>();
	Evaluation e{g};
	SerialScheduler{}.run(g, e);

	const auto& node = static_cast<const DescribeNode&>(g.node(n));
	const auto described = [&](PortId port)
	{ return e.describe(PortAddress{n, port}); };

	REQUIRE(described(node.i) == "42");								 // int via ostream
	REQUIRE(described(node.b) == "true");							 // bool special-case
	REQUIRE(described(node.s) == "hi");								 // std::string via ostream
	REQUIRE(described(node.st) == "stamped!");						 // member toString()
	REQUIRE(described(node.op).find("Opaque") != std::string::npos); // fallback
}
