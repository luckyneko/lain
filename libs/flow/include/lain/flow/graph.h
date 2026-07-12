#pragma once

#include "lain/flow/boundary.h" // BoundaryInput / BoundaryOutput — the enumerated pin handles
#include "lain/flow/node.h"
#include "lain/flow/types.h"

#include <cstddef>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace lain::flow
{
	// Owns the nodes and the edges between their ports: a pure data model. Builds
	// the DAG (add / connect / disconnect) and exposes a dependency-respecting
	// order. It does not execute — a Scheduler (see scheduler.h) consumes a Graph
	// and evaluates it (push run, or pull of one node's upstream).
	class Graph
	{
	public:
		// Construct a node of type T (must derive from Node) in place; returns its id.
		template <typename T, typename... Args>
		NodeId add(Args&&... args);

		// Adopt an already-constructed node (e.g. produced by a factory); returns its
		// id. The type-erased entry point the template add<T> forwards to.
		NodeId add(std::unique_ptr<Node> node);

		// Remove a node and every edge touching it (in or out); returns whether it
		// existed. Other nodes keep their ids — ids are handles, not positions.
		bool removeNode(NodeId id);

		// Mark every node dirty so the next Scheduler::run recomputes the whole graph — the
		// "force a full refresh" over the normal incremental run (which recomputes only dirty nodes
		// + their downstream). The structural mutations here (connect / disconnect / removeNode /
		// removePort) already mark the affected downstream node dirty; this is for a caller that
		// wants everything recomputed regardless.
		void markAllDirty();

		std::size_t nodeCount() const { return m_nodes.size(); }
		bool contains(NodeId id) const { return m_nodes.count(id) != 0; }

		// Every node id, in ascending id order — a stable enumeration of all nodes (unlike
		// topoOrder, which is dependency order). The order a serializer walks nodes in.
		std::vector<NodeId> nodeIds() const;
		Node& node(NodeId id) { return *m_nodes.at(id); }
		const Node& node(NodeId id) const { return *m_nodes.at(id); }

		// Connect an output to an input. Type-checked (declared types must match),
		// single-source (an input takes one edge), and cycle-rejecting — so the
		// graph stays acyclic by construction. The edge stores the ports' stable
		// PortAddresses, so it survives the ports being reordered/renumbered.
		Connection connect(PortAddress from, PortAddress to);

		// Index convenience: connect the from-node's `outPort`-th output to the to-node's
		// `inPort`-th input. Resolves the indices to PortAddresses *now* (the edge stores the
		// stable ids, never the indices), so it is safe for building fixed graphs / tests.
		Connection connect(NodeId from, PortIndex outPort, NodeId to, PortIndex inPort);

		// Remove the edge feeding `input`, if any; returns whether one was removed.
		bool disconnect(PortAddress input);
		bool disconnect(NodeId to, PortIndex inPort); // index convenience (resolves now)

		// Remove a dynamic port. A *primitive*: it REFUSES (returns false) if any edge still
		// touches the port, keeping the no-dangling-edge invariant total — the safe gesture
		// edit::removePort disconnects the incident edges first. Returns whether the port was
		// removed.
		bool removePort(PortAddress port);

		// A directed edge: an output PortAddress feeding an input PortAddress. Addressed by
		// stable PortId, never PortIndex, so it survives dynamic-port mutation.
		struct Edge
		{
			PortAddress from;
			PortAddress to;
		};
		const std::vector<Edge>& edges() const { return m_edges; }

		// Nodes in a dependency-respecting order (sources first). Acyclic by
		// construction, so this always covers every node. Cached and recomputed
		// lazily after a topology change.
		const std::vector<NodeId>& topoOrder() const;

		// The graph's I/O boundary — a flat list of bindable pins across every boundary node
		// (see boundary.h). A host loops these to setValue each input, run, then read each
		// output's value(); the RTTI to find the nodes lives here, not scattered across cli +
		// gui. Node insertion order then pin order, so the interface list is stable.
		std::vector<BoundaryInput> boundaryInputs();
		std::vector<BoundaryOutput> boundaryOutputs();

		// The graph's single boundary input / output node (or nullptr if none) — for a host that
		// edits the interface (the ± add/remove pins), where the flat pin lists above are for a
		// host that just binds. A graph has one of each (Blender-style: one Group Input, one Group
		// Output, each with N pins); if several exist the first is returned. Keeps the RTTI here.
		GroupInputNode* boundaryInputNode();
		GroupOutputNode* boundaryOutputNode();

	private:
		bool valid(NodeId id) const { return m_nodes.count(id) != 0; }
		// Is `target` reachable from `start` by following edges (start included)?
		bool reaches(NodeId start, NodeId target) const;

		// Nodes keyed by id, not stored by position — an id outlives the removal of
		// other nodes. Ordered (by the monotonic counter, i.e. insertion order) so
		// iteration and topo order stay deterministic.
		std::map<NodeId, std::unique_ptr<Node>> m_nodes;
		NodeId m_nextId{1}; // 0 is the reserved sentinel; real ids start at 1
		std::vector<Edge> m_edges;
		mutable std::vector<NodeId> m_topo;
		mutable bool m_topoValid = false;
	};
} // namespace lain::flow

#include "lain/flow/details/graph.inl"
