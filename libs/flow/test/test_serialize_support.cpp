// The three flow-core additions that name-addressed graph serialization relies on: port-name
// uniqueness (the query + the dynamic-side reject), the port-type registry's reverse lookup, and
// the empty-dynamic-side contract of a factory-fresh boundary node. (The static-port duplicate is
// a debug assert — an author bug — so it is not exercised here.)

#include <lain/flow/boundary.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>
#include <typeindex>
#include <typeinfo>

using lain::flow::GroupInputNode;
using lain::flow::GroupOutputNode;
using lain::flow::Port;
using lain::flow::PortId;

TEST_CASE("hasPortNamed is direction-scoped", "[flow]")
{
	GroupInputNode gi; // its boundary pins are OUTPUTs
	gi.addBoundary<int>("x");
	REQUIRE(gi.hasPortNamed(Port::Direction::Output, "x"));
	REQUIRE_FALSE(gi.hasPortNamed(Port::Direction::Output, "y"));
	REQUIRE_FALSE(gi.hasPortNamed(Port::Direction::Input, "x")); // other direction is free
}

TEST_CASE("addDynamicPort rejects a duplicate name on the dynamic side", "[flow]")
{
	GroupInputNode gi;
	const PortId a = gi.addBoundary<int>("a");
	REQUIRE(a != PortId{});

	const PortId dup = gi.addBoundary<int>("a"); // same name -> rejected (null PortId)
	REQUIRE(dup == PortId{});
	REQUIRE(gi.boundaryCount() == 1); // not added

	const PortId b = gi.addBoundary<int>("b"); // a fresh name is fine
	REQUIRE(b != PortId{});
	REQUIRE(gi.boundaryCount() == 2);
}

TEST_CASE("validPortName: a letter, then alphanumeric / underscore", "[flow]")
{
	REQUIRE(lain::flow::validPortName("source"));
	REQUIRE(lain::flow::validPortName("a"));
	REQUIRE(lain::flow::validPortName("In_1"));
	REQUIRE(lain::flow::validPortName("x2y"));

	REQUIRE_FALSE(lain::flow::validPortName(""));		 // empty
	REQUIRE_FALSE(lain::flow::validPortName("2x"));		 // leading digit
	REQUIRE_FALSE(lain::flow::validPortName("_x"));		 // leading underscore
	REQUIRE_FALSE(lain::flow::validPortName("my name")); // space
	REQUIRE_FALSE(lain::flow::validPortName("a-b"));	 // punctuation
}

TEST_CASE("addDynamicPort rejects an invalid name", "[flow]")
{
	GroupInputNode gi;
	REQUIRE(gi.addBoundary<int>("2bad") == PortId{}); // leading digit -> rejected
	REQUIRE(gi.addBoundary<int>("has space") == PortId{});
	REQUIRE(gi.boundaryCount() == 0);

	REQUIRE(gi.addBoundary<int>("good") != PortId{}); // a valid name is added
	REQUIRE(gi.boundaryCount() == 1);
}

TEST_CASE("a factory-fresh boundary node has an empty dynamic side", "[flow]")
{
	GroupInputNode gi; // dynamic side = outputs
	REQUIRE(gi.outputCount() == 0);
	GroupOutputNode go; // dynamic side = inputs
	REQUIRE(go.inputCount() == 0);
}

// Test-only value types, unique so registering one doesn't collide with the process-wide registry.
struct RevProbe
{
};
struct NeverRegistered
{
};

TEST_CASE("the port-type registry reverse-looks-up a key from a type", "[flow]")
{
	lain::flow::registerPortType<RevProbe>("RevProbe");
	REQUIRE(lain::flow::portTypeKey(std::type_index(typeid(RevProbe))) == "RevProbe");
	// an unregistered type -> empty (the serializer records that as an issue)
	REQUIRE(lain::flow::portTypeKey(std::type_index(typeid(NeverRegistered))).empty());
}
