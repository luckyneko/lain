// Unit tests for Port::describe() — the flow side of the value-display pathway: the
// PortType flyweight's type-erasure bridge over lain::meta::toString, plus the empty
// slot. The ladder itself is covered by (meta) test_tostring; here we confirm a Port
// renders its type-erased value through it. GPU-free.

#include "lain/flow/graph.h"
#include "lain/flow/node.h"

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
		DescribeNode()
			: Node("Describe")
		{
			addOutput<int>("i");
			addOutput<bool>("b");
			addOutput<std::string>("s");
			addOutput<Stamped>("st");
			addOutput<Opaque>("op");
		}
		void compute() override
		{
			output(0).set(42);
			output(1).set(true);
			output(2).set(std::string("hi"));
			output(3).set(Stamped{});
			output(4).set(Opaque{7});
		}
	};
} // namespace lain::flow::test

using namespace lain::flow::test;

TEST_CASE("Port::describe renders an unset port as empty", "[describe]")
{
	Graph g;
	const NodeId n = g.add<DescribeNode>();
	REQUIRE(g.node(n).output(0).describe() == "(empty)"); // before compute: no value
}

TEST_CASE("Port::describe bridges a type-erased value through meta::toString", "[describe]")
{
	Graph g;
	const NodeId n = g.add<DescribeNode>();
	g.node(n).compute();
	const Node& node = g.node(n);

	REQUIRE(node.output(0).describe() == "42");								// int via ostream
	REQUIRE(node.output(1).describe() == "true");							// bool special-case
	REQUIRE(node.output(2).describe() == "hi");								// std::string via ostream
	REQUIRE(node.output(3).describe() == "stamped!");						// member toString()
	REQUIRE(node.output(4).describe().find("Opaque") != std::string::npos); // fallback
}
