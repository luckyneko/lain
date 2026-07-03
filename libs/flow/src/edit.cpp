#include "lain/flow/edit.h"

#include "lain/flow/node.h"

#include <optional>
#include <utility>

namespace lain::flow::edit
{
	// The edge currently feeding (to, inPort), if any. An input takes a single source,
	// so at most one edge matches.
	static std::optional<Graph::Edge> findEdgeInto(const Graph& graph, NodeId to, PortIndex inPort)
	{
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to == to && e.inPort == inPort)
				return e;
		}
		return std::nullopt;
	}

	bool connectReplacing(Graph& graph, NodeId from, PortIndex outPort, NodeId to, PortIndex inPort)
	{
		// Free input: a plain connect suffices.
		const std::optional<Graph::Edge> existing = findEdgeInto(graph, to, inPort);
		if (!existing)
			return graph.connect(from, outPort, to, inPort) == Connection::Ok;

		// Occupied input: free it, then try the new edge. Removing an edge *into* `to`
		// can't change what `to` reaches, so the cycle verdict is the same as it would
		// have been with the old edge still present.
		graph.disconnect(to, inPort);
		if (graph.connect(from, outPort, to, inPort) == Connection::Ok)
			return true;

		// Rejected — restore the original edge so a failed replace is a no-op, not a
		// silent delete. The restore always succeeds: the input is free again, the type
		// still matches, and re-adding an edge that was acyclic before (nothing else
		// changed) stays acyclic.
		graph.connect(existing->from, existing->outPort, to, inPort);
		return false;
	}

	bool disconnect(Graph& graph, NodeId to, PortIndex inPort)
	{
		return graph.disconnect(to, inPort);
	}

	bool remove(Graph& graph, const std::vector<NodeId>& nodes, const std::vector<Graph::Edge>& edges)
	{
		bool removed = false;
		// Edges first (by stable destination), then nodes. removeNode drops a node's
		// incident edges too, so a co-selected edge on a removed node just no-ops here.
		for (const Graph::Edge& e : edges)
			removed |= graph.disconnect(e.to, e.inPort);
		for (const NodeId n : nodes)
			removed |= graph.removeNode(n);
		return removed;
	}

	NodeId addNode(Graph& graph, std::unique_ptr<Node> node)
	{
		return graph.add(std::move(node));
	}
} // namespace lain::flow::edit
