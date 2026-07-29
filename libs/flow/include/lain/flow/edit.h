#pragma once

// The editing layer for lain::flow: stateless free functions over a Graph, peer to
// the Scheduler (execution) layer. They compose Graph's invariant-preserving
// primitives (connect / disconnect / removeNode / add) with editor *policy* and
// *gesture orchestration* — the part that varies across front-ends, kept off the
// pure-data Graph. A GUI front-end is an adapter: it decodes its own input (pins,
// selection, cursor) into these calls, so the edit logic is tested against a plain
// Graph with no GUI in the loop (see test/test_edit.cpp).

#include "lain/flow/graph.h" // Graph, Graph::Edge, NodeId, PortAddress, Connection

namespace lain::flow::edit
{
	// Connect output `from` -> input `to`, replacing whatever currently feeds the input.
	// Atomic: if the new edge is rejected (type mismatch, cycle, invalid node/port), the
	// existing edge is left untouched. Returns whether the input ends up fed by `from`.
	//
	// Why atomic matters: Graph::connect reports InputInUse *before* WouldCycle, so a
	// naive disconnect-then-connect can drop the old edge and then fail the cycle
	// check, leaving the input empty. This captures the original and restores it on
	// failure instead.
	bool connectReplacing(Graph& graph, PortAddress from, PortAddress to);
	// Index convenience (resolves the positions to addresses now, like Graph::connect).
	bool connectReplacing(Graph& graph, NodeId from, PortIndex outPort, NodeId to, PortIndex inPort);

	// Remove the edge feeding `input`, if any (the detach gesture). A thin route to
	// Graph::disconnect, kept so every mutation crosses one seam. Returns whether an edge
	// was removed.
	bool disconnect(Graph& graph, PortAddress input);
	bool disconnect(Graph& graph, NodeId to, PortIndex inPort); // index convenience

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

	// Add a dynamic pin of the registered port type `typeKey` to `node` (the "+" gesture): a
	// route through the port-type registry, on the mutation seam. Returns the new PortId, or a
	// null PortId if `node` isn't a DynamicPortsNode or `typeKey` isn't registered.
	PortId addPort(Graph& graph, NodeId node, const std::string& typeKey, std::string name);

	// Remove a dynamic port safely (the "×" gesture): disconnect every edge touching it (an
	// output may feed several inputs; an input has one source), then remove the now-free port via
	// the Graph::removePort primitive. Returns whether the port was removed.
	bool removePort(Graph& graph, PortAddress port);

	// What a syncGroupPorts pass changed. `disconnected` is the one a host must SURFACE: those are
	// the parent's edges that a pin disappearing from the group's interface took with it.
	struct GroupSync
	{
		int added = 0;		  // outer ports created for new inner boundary pins
		int removed = 0;	  // outer ports dropped because their inner pin is gone
		int renamed = 0;	  // outer ports retitled to match a renamed inner pin
		int disconnected = 0; // parent edges cut by those removals

		bool changed() const { return added != 0 || removed != 0 || renamed != 0; }
	};

	// Reconcile a group node's own ports against its inner graph's boundary pins — the GESTURE
	// over GroupNode::exposePort. A group's interface IS its inner graph's boundary, so this runs
	// after anything that could have changed it (editing the inner interface, loading, relinking a
	// template).
	//
	// Identity is the outer<->inner PortId mapping, never the name: a renamed inner pin keeps its
	// outer port and its wiring, and is merely retitled. A pin that VANISHED takes its outer port
	// with it — and because that port may still be wired in the parent, the removal goes through
	// edit::removePort (disconnect, then the refusing primitive), which is exactly why this is a
	// gesture in the editing layer and not a method on the node: only the parent Graph can cut
	// those edges.
	//
	// Order is remove -> rename -> add, so a name freed by a removal or a rename is available to a
	// new pin in the same pass. A node that contains no graph is a no-op.
	GroupSync syncGroupPorts(Graph& parent, NodeId group);
} // namespace lain::flow::edit
