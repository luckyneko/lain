#pragma once

// The editing layer for lain::flow: stateless free functions over a Graph, peer to
// the Scheduler (execution) layer. They compose Graph's invariant-preserving
// primitives (connect / disconnect / removeNode / add) with editor *policy* and
// *gesture orchestration* — the part that varies across front-ends, kept off the
// pure-data Graph. A GUI front-end is an adapter: it decodes its own input (pins,
// selection, cursor) into these calls, so the edit logic is tested against a plain
// Graph with no GUI in the loop (see test/test_edit.cpp).

#include "lain/flow/graph.h" // Graph, Graph::Edge, NodeId, PortAddress, Connection

namespace lain::flow
{
	class GroupNode; // replaceGroup takes one by pointer — group.h is the .cpp's business
}

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
	bool connectReplacing(Graph& graph, NodeId from, std::size_t outPort, NodeId to, std::size_t inPort);

	// Remove the edge feeding `input`, if any (the detach gesture). A thin route to
	// Graph::disconnect, kept so every mutation crosses one seam. Returns whether an edge
	// was removed.
	bool disconnect(Graph& graph, PortAddress input);
	bool disconnect(Graph& graph, NodeId to, std::size_t inPort); // index convenience

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

	// Why a group gesture did nothing. An enum rather than a message, like Connection: the reason is
	// a fact about the graph, and the wording belongs to whichever host is reporting it.
	//
	// Every refusal is ATOMIC — the graph is untouched — which is why they are all detected up front,
	// before the first node moves. A half-formed group is not something a user can undo their way out
	// of by hand.
	enum class GroupRefusal
	{
		None,			  // it went ahead
		EmptySelection,	  // nothing to group / no such node
		ContainsBoundary, // the selection holds a boundary node: grouping the graph's own interface
						  // would leave the document with no way in or out
		UnnamedPinType,	  // a value crossing the selection boundary has a type with no port-type
						  // registry key, so the boundary pin it needs could not be SERIALIZED — the
						  // group would work until saved and then come back missing that pin and its
						  // wiring. Register the type (registerPortType<T>) and the gesture succeeds.
		WouldCycle,		  // a value leaves the selection, passes through nodes outside it, and comes
						  // back in. Contracting that to one node needs it to run both before and
						  // after those nodes — so the selection has to be widened (or narrowed) to
						  // take them in. The graph itself was, and stays, acyclic.
		NotAGroup,		  // ungroup: that node contains no graph
		NotInline,		  // ungroup: that node is a LINKED group — its interior is its template's, and
						  // is shared by every other instance, so it cannot be spliced into this parent
	};

	// What groupSelected did.
	struct GroupResult
	{
		NodeId group; // the new group node — null unless ok()
		GroupRefusal refusal = GroupRefusal::None;
		std::vector<NodeId> moved; // the nodes now living inside it, in graph order
		int inputPins = 0;		   // boundary pins created for values entering the selection
		int outputPins = 0;		   // ... and for values leaving it

		bool ok() const { return refusal == GroupRefusal::None; }
	};

	// Move `selection` into a new INLINE group node, computing the CUT-SET: the wiring that crossed
	// the selection's edge becomes the new group's interface, so the graph computes exactly what it
	// did before. The inverse of ungroup.
	//
	// - Each distinct outer output port feeding the selection becomes ONE boundary input pin, so a
	//   fan-out from one source arrives as one pin feeding several inner nodes rather than as a spray
	//   of duplicates carrying the same value.
	// - Each distinct inner output port feeding outside becomes ONE boundary output pin, for the
	//   mirror-image reason.
	// - Pin names are taken from the port being mirrored and uniquified, since a name is how an edge
	//   is addressed on disk and how a boundary pin is named on the cli.
	// - Edges wholly inside the selection move with it; edges wholly outside are untouched.
	//
	// Nodes keep their ids across the move (Graph::extract), so a host's per-node metadata — canvas
	// position, preview keys — can follow them down a level by the key it already has.
	//
	// Placement of the new node is the host's business (it is editor metadata, not graph structure):
	// this returns the id, and the host positions it.
	GroupResult groupSelected(Graph& parent, const std::vector<NodeId>& selection);

	// What ungroup did.
	struct UngroupResult
	{
		GroupRefusal refusal = GroupRefusal::None;
		std::vector<NodeId> moved; // the nodes lifted into the parent, in inner-graph order
		int reconnected = 0;	   // parent edges re-formed directly, in place of the two-hop route
								   // through the group's boundary

		bool ok() const { return refusal == GroupRefusal::None; }
	};

	// Splice an INLINE group's interior into its parent and delete the group — the inverse of
	// groupSelected. Each boundary pin is resolved back to the direct edges it stood for: whatever fed
	// the group's outer input now feeds the inner consumers of the matching inner pin, and whatever
	// produced an outer output now feeds that output's consumers.
	//
	// The inner nodes keep their ids, exactly as in groupSelected — the move is the same primitive in
	// the other direction. Refuses a LINKED group: its interior is the template's definition, shared
	// with every other instance of it (ADR-0013), so there is nothing here to splice that this
	// document owns. Make it local first.
	UngroupResult ungroup(Graph& parent, NodeId group);

	// What replaceGroup did.
	struct ReplaceResult
	{
		GroupRefusal refusal = GroupRefusal::None;
		int reconnected = 0; // parent edges carried across to the replacement
		int dropped = 0;	 // parent edges the replacement's interface has no matching pin for

		bool ok() const { return refusal == GroupRefusal::None; }
	};

	// Swap what backs a group node — an inline body for a link to a template, or the reverse — keeping
	// the group itself. This is the shared half of "Save as Template" and "Make Local": both change
	// only where a group's recipe is STORED, and neither is meant to disturb the document around it.
	//
	// `replacement` must already carry its interior (the caller loaded the template, or built the
	// body); this mirrors that interior outward through syncGroupPorts and re-attaches the parent.
	//
	// The replacement KEEPS THE ORIGINAL NodeId. Not an optimisation — it is the honest answer: from
	// the document's point of view this is the same group, differently backed, so everything keyed on
	// that id (canvas position, the imnodes int, preview keys, the layout subtree) stays pointing at
	// it. Minting a fresh id would scatter all of that for no gain.
	//
	// The parent's edges are carried across by PIN NAME, because the replacement's PortIds are its own
	// and there is nothing else the two interfaces share. Both kinds derive their port names from the
	// same inner boundary, so an unchanged interface reconnects completely; a pin the new interface
	// lacks is reported in `dropped` rather than silently lost, since that is a real change to the
	// document a host should be able to surface.
	ReplaceResult replaceGroup(Graph& parent, NodeId group, std::unique_ptr<GroupNode> replacement);
} // namespace lain::flow::edit
