#pragma once

// The editing layer for lain::flow: stateless free functions over a Graph, peer to
// the Scheduler (execution) layer. They compose Graph's invariant-preserving
// primitives (connect / disconnect / removeNode / add) with editor *policy* and
// *gesture orchestration* — the part that varies across front-ends, kept off the
// pure-data Graph. A GUI front-end is an adapter: it decodes its own input (pins,
// selection, cursor) into these calls, so the edit logic is tested against a plain
// Graph with no GUI in the loop (see test/test_edit.cpp).

#include <lain/flow/graph.h> // Graph, Graph::Edge, NodeId, PortIndex, Connection

namespace lain::flow::edit
{
	// Connect (from, outPort) -> (to, inPort), replacing whatever currently feeds the
	// input. Atomic: if the new edge is rejected (type mismatch, cycle, invalid
	// node/port), the existing edge is left untouched. Returns whether the input ends
	// up fed by (from, outPort).
	//
	// Why atomic matters: Graph::connect reports InputInUse *before* WouldCycle, so a
	// naive disconnect-then-connect can drop the old edge and then fail the cycle
	// check, leaving the input empty. This captures the original and restores it on
	// failure instead.
	bool connectReplacing(Graph& graph, NodeId from, PortIndex outPort, NodeId to, PortIndex inPort);

	// Remove the edge feeding (to, inPort), if any (the detach gesture). A thin route
	// to Graph::disconnect, kept so every mutation crosses one seam. Returns whether an
	// edge was removed.
	bool disconnect(Graph& graph, NodeId to, PortIndex inPort);

	// Delete a selection in one gesture: remove every node in `nodes` and every edge in
	// `edges`. Edges are identified by value (their stable destination), never by list
	// position, so the removals can't invalidate one another. removeNode already drops a
	// node's incident edges, so a co-selected edge on a removed node is a harmless
	// no-op. Returns whether anything was removed.
	bool remove(Graph& graph, const std::vector<NodeId>& nodes, const std::vector<Graph::Edge>& edges);

	// Adopt an already-constructed node (e.g. from a factory) into the graph. A thin
	// route to Graph::add on the same seam; placement is the adapter's job. Returns the
	// new node's id.
	NodeId addNode(Graph& graph, std::unique_ptr<Node> node);
} // namespace lain::flow::edit
