// Unit tests for lain::flow::edit — the editing layer's policy and orchestration
// over a plain Graph. GPU-free (shared fixtures from testnodes.h), so the whole edit
// surface is exercised without a driver: this is the point of extracting it from the
// ImGui frame.

#include "testnodes.h"

#include <lain/flow/edit.h>
#include <lain/flow/graph.h>

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace lain::flow;
using namespace lain::flow::test;

// The edge feeding (to, inPort), or nullptr when the input is free.
static const Graph::Edge* edgeInto(const Graph& g, NodeId to, PortIndex inPort)
{
	for (const Graph::Edge& e : g.edges())
	{
		if (e.to == to && e.inPort == inPort)
			return &e;
	}
	return nullptr;
}

TEST_CASE("connectReplacing connects a free input", "[edit]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);
	const NodeId add = g.add<AddInt>();

	REQUIRE(edit::connectReplacing(g, c, 0, add, 0));
	REQUIRE(g.edges().size() == 1);
	REQUIRE(edgeInto(g, add, 0) != nullptr);
	REQUIRE(edgeInto(g, add, 0)->from == c);
}

TEST_CASE("connectReplacing replaces an occupied input", "[edit]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(1);
	const NodeId c2 = g.add<ConstInt>(2);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);

	REQUIRE(edit::connectReplacing(g, c2, 0, add, 0)); // drag c2 onto the occupied input
	REQUIRE(g.edges().size() == 1);					   // still single-source
	REQUIRE(edgeInto(g, add, 0)->from == c2);		   // now fed by c2, not c1
}

TEST_CASE("connectReplacing preserves the original edge when the replacement would cycle", "[edit]")
{
	// A -> B -> C; dragging C's output onto B's occupied input would cycle (B reaches C).
	// The naive disconnect-then-connect would have dropped A -> B and then failed the
	// cycle check, leaving B's input empty. connectReplacing must restore A -> B.
	Graph g;
	const NodeId a = g.add<AddInt>();
	const NodeId b = g.add<AddInt>();
	const NodeId c = g.add<AddInt>();
	REQUIRE(g.connect(a, 0, b, 0) == Connection::Ok); // A -> B (occupies B.in0)
	REQUIRE(g.connect(b, 0, c, 0) == Connection::Ok); // B -> C

	REQUIRE_FALSE(edit::connectReplacing(g, c, 0, b, 0)); // C -> B would cycle: rejected
	REQUIRE(g.edges().size() == 2);						  // nothing added or lost
	REQUIRE(edgeInto(g, b, 0) != nullptr);
	REQUIRE(edgeInto(g, b, 0)->from == a); // A -> B restored, not left empty
}

TEST_CASE("connectReplacing rejects a type mismatch without touching the graph", "[edit]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);	// int output
	const NodeId sink = g.add<SinkFloat>(); // float input

	REQUIRE_FALSE(edit::connectReplacing(g, c, 0, sink, 0));
	REQUIRE(g.edges().empty());
}

TEST_CASE("connectReplacing rejects invalid nodes and ports", "[edit]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);
	const NodeId add = g.add<AddInt>();

	REQUIRE_FALSE(edit::connectReplacing(g, NodeId{99}, 0, add, 0)); // no such source
	REQUIRE_FALSE(edit::connectReplacing(g, c, 5, add, 0));			 // out-of-range output
	REQUIRE(g.edges().empty());
}

TEST_CASE("remove deletes selected nodes and their incident edges", "[edit]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(1);
	const NodeId c2 = g.add<ConstInt>(2);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, add, 1) == Connection::Ok);

	REQUIRE(edit::remove(g, {c1}, {}));
	REQUIRE(g.nodeCount() == 2);
	REQUIRE(g.edges().size() == 1);		   // c1 -> add went with c1
	REQUIRE(g.edges().front().from == c2); // c2 -> add survives
}

TEST_CASE("remove deletes selected edges, leaving nodes in place", "[edit]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c, 0, add, 0) == Connection::Ok);

	const std::vector<Graph::Edge> edges = g.edges(); // resolve before mutating
	REQUIRE(edit::remove(g, {}, edges));
	REQUIRE(g.edges().empty());
	REQUIRE(g.nodeCount() == 2); // both nodes remain
}

TEST_CASE("remove tolerates an edge co-selected with the node it touches", "[edit]")
{
	Graph g;
	const NodeId c1 = g.add<ConstInt>(1);
	const NodeId c2 = g.add<ConstInt>(2);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c1, 0, add, 0) == Connection::Ok);
	REQUIRE(g.connect(c2, 0, add, 1) == Connection::Ok);

	// Delete c1 *and* the c1 -> add edge together: removeNode already drops the edge, so
	// the explicit disconnect is a harmless no-op — no double-remove, no crash.
	const std::vector<Graph::Edge> edges = {Graph::Edge{c1, 0, add, 0}};
	REQUIRE(edit::remove(g, {c1}, edges));
	REQUIRE(g.nodeCount() == 2);
	REQUIRE(g.edges().size() == 1);
	REQUIRE(g.edges().front().from == c2);
}

TEST_CASE("remove of an empty selection changes nothing", "[edit]")
{
	Graph g;
	g.add<ConstInt>(1);
	REQUIRE_FALSE(edit::remove(g, {}, {}));
	REQUIRE(g.nodeCount() == 1);
}

TEST_CASE("disconnect removes a single edge", "[edit]")
{
	Graph g;
	const NodeId c = g.add<ConstInt>(1);
	const NodeId add = g.add<AddInt>();
	REQUIRE(g.connect(c, 0, add, 0) == Connection::Ok);

	REQUIRE(edit::disconnect(g, add, 0));
	REQUIRE_FALSE(edit::disconnect(g, add, 0)); // nothing left to remove
	REQUIRE(g.edges().empty());
}

TEST_CASE("addNode adopts a constructed node and returns its id", "[edit]")
{
	Graph g;
	const NodeId id = edit::addNode(g, std::make_unique<AddInt>());
	REQUIRE(g.nodeCount() == 1);
	REQUIRE(g.node(id).name() == "Add");
	REQUIRE(g.node(id).id() == id);
}
