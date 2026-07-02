#pragma once

#include <lain/flow/node.h>
#include <lain/flow/types.h>

#include <cstddef>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace lain::task
{
	class Executor; // only run(Executor&) names it; full type via <lain/task/task.h>
}

namespace lain::flow
{
	// Owns the nodes and the edges between their ports. Builds the DAG (add /
	// connect / disconnect) and exposes a dependency-respecting order; the
	// scheduler (step 4) lowers that onto lain::task to actually run it.
	class Graph
	{
	public:
		// Construct a node of type T (must derive from Node) in place; returns its id.
		template <typename T, typename... Args>
		NodeId add(Args&&... args);

		std::size_t nodeCount() const { return m_nodes.size(); }
		Node& node(NodeId id) { return *m_nodes.at(id); }
		const Node& node(NodeId id) const { return *m_nodes.at(id); }

		// Connect an output to an input. Type-checked (declared types must match),
		// single-source (an input takes one edge), and cycle-rejecting — so the
		// graph stays acyclic by construction.
		Connection connect(NodeId from, PortIndex outPort, NodeId to, PortIndex inPort);

		// Remove the edge feeding (to, inPort), if any; returns whether one was removed.
		bool disconnect(NodeId to, PortIndex inPort);

		struct Edge
		{
			NodeId from;
			PortIndex outPort;
			NodeId to;
			PortIndex inPort;
		};
		const std::vector<Edge>& edges() const { return m_edges; }

		// Nodes in a dependency-respecting order (sources first). Acyclic by
		// construction, so this always covers every node. Cached and recomputed
		// lazily after a topology change.
		const std::vector<NodeId>& topoOrder() const;

		// Push: evaluate the whole graph — each node fires once its inputs are
		// ready — lowering the DAG onto the given lain::task executor (caller-owned,
		// so worker count and lifetime stay explicit).
		void run(lain::task::Executor& executor);

		// Pull: evaluate just `target`'s upstream subgraph on demand, recomputing
		// only dirty nodes. The entry point for constant / on-request sources.
		void evaluate(NodeId target);

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

#include <lain/flow/details/graph.inl>
