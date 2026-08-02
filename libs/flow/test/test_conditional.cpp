// Conditional / gated eval (WORK.md Tier A #2, ADR-0007): a node is READY iff every REQUIRED input
// has a value. A Gate suppresses by producing no value; that emptiness propagates through nodes with
// a required input (they don't compute, their outputs cleared); an OPTIONAL input (a Select/Merge
// branch) doesn't block, so its compute() picks a live one. "Skip" is just absence-of-value.
// Exercises the mechanism with test nodes; the concrete Gate/Select/Merge nodes are a later slice.

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/port.h" // Presence
#include "lain/flow/scheduler.h"

#include <catch2/catch_test_macros.hpp>

using lain::flow::Connection;
using lain::flow::Graph;
using lain::flow::NodeId;
using lain::flow::Presence;
using lain::flow::SerialScheduler;

// A gate-as-source: emits `value` when `pass`, else produces NO value (clear) — the suppress signal.
class TestGate : public lain::flow::Node
{
public:
	TestGate()
		: lain::flow::Node("Gate")
	{
		m_out = addOutput<int>("out");
	}
	void compute() override
	{
		if (pass)
			output(m_out).set<int>(value);
		else
			output(m_out).clear();
	}
	bool pass = true;
	int value = 0;

private:
	lain::flow::PortId m_out;
};

// A passthrough with a REQUIRED input — counts computes, so a test can see it get suppressed.
class CountingRelay : public lain::flow::Node
{
public:
	CountingRelay()
		: lain::flow::Node("Relay")
	{
		m_in = addInput<int>("in"); // Required (default): an empty input keeps the node from being ready
		m_out = addOutput<int>("out");
	}
	void compute() override
	{
		++computes; // reached only when the required input has a value
		output(m_out).set<int>(input(m_in).get<int>());
	}
	int computes = 0;

private:
	lain::flow::PortId m_in;
	lain::flow::PortId m_out;
};

// A select with two OPTIONAL branch inputs — outputs the first one that has a value, else nothing.
class TestSelect : public lain::flow::Node
{
public:
	TestSelect()
		: lain::flow::Node("Select")
	{
		m_a = addInput<int>("a", Presence::Optional);
		m_b = addInput<int>("b", Presence::Optional);
		m_out = addOutput<int>("out");
	}
	void compute() override
	{
		if (input(m_a).ready())
			output(m_out).set<int>(input(m_a).get<int>());
		else if (input(m_b).ready())
			output(m_out).set<int>(input(m_b).get<int>());
		else
			output(m_out).clear();
	}

private:
	lain::flow::PortId m_a;
	lain::flow::PortId m_b;
	lain::flow::PortId m_out;
};

TEST_CASE("an empty required input suppresses the node, and that propagates", "[flow][conditional]")
{
	Graph graph;
	const NodeId g = graph.add<TestGate>();
	const NodeId r = graph.add<CountingRelay>();
	const NodeId k = graph.add<CountingRelay>();
	REQUIRE(graph.connect(g, 0, r, 0) == Connection::Ok);
	REQUIRE(graph.connect(r, 0, k, 0) == Connection::Ok);

	auto& gate = static_cast<TestGate&>(graph.node(g));
	auto& relay = static_cast<CountingRelay&>(graph.node(r));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));
	gate.pass = false; // produce no value
	gate.value = 5;

	SerialScheduler scheduler;
	scheduler.run(graph);

	REQUIRE(relay.computes == 0); // required input empty -> not ready -> never computed
	REQUIRE(sink.computes == 0);
	REQUIRE_FALSE(graph.node(r).output(0).ready()); // its outputs were cleared
	REQUIRE_FALSE(graph.node(k).output(0).ready());
}

TEST_CASE("giving the gate a value resurrects the suppressed subtree", "[flow][conditional]")
{
	Graph graph;
	const NodeId g = graph.add<TestGate>();
	const NodeId r = graph.add<CountingRelay>();
	const NodeId k = graph.add<CountingRelay>();
	graph.connect(g, 0, r, 0);
	graph.connect(r, 0, k, 0);

	auto& gate = static_cast<TestGate&>(graph.node(g));
	auto& relay = static_cast<CountingRelay&>(graph.node(r));
	auto& sink = static_cast<CountingRelay&>(graph.node(k));
	gate.pass = false;
	gate.value = 5;

	SerialScheduler scheduler;
	scheduler.run(graph); // suppressed
	REQUIRE(relay.computes == 0);

	gate.pass = true;
	graph.node(g).markDirty();
	scheduler.run(graph); // gate dirty -> closure pulls the subtree back in -> now ready

	REQUIRE(relay.computes == 1);
	REQUIRE(sink.computes == 1);
	REQUIRE(graph.node(r).output(0).ready());
	REQUIRE(graph.node(k).output(0).get<int>() == 5); // the value flowed all the way through
}

TEST_CASE("an optional input does not block; the node picks a live branch", "[flow][conditional]")
{
	Graph graph;
	const NodeId ga = graph.add<TestGate>();
	const NodeId gb = graph.add<TestGate>();
	const NodeId sel = graph.add<TestSelect>();
	REQUIRE(graph.connect(ga, 0, sel, 0) == Connection::Ok); // -> "a" (optional)
	REQUIRE(graph.connect(gb, 0, sel, 1) == Connection::Ok); // -> "b" (optional)

	static_cast<TestGate&>(graph.node(ga)).pass = false;  // branch a produces nothing
	auto& gateB = static_cast<TestGate&>(graph.node(gb)); // branch b lives
	gateB.pass = true;
	gateB.value = 7;

	SerialScheduler scheduler;
	scheduler.run(graph);

	REQUIRE(graph.node(sel).output(0).ready());			// Select computed (an empty optional didn't block)
	REQUIRE(graph.node(sel).output(0).get<int>() == 7); // picked the live branch b
}
