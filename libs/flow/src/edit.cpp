#include "lain/flow/edit.h"

#include "lain/flow/dynamicports.h"
#include "lain/flow/node.h"
#include "lain/flow/porttyperegistry.h"

#include <optional>
#include <utility>
#include <vector>

namespace lain::flow::edit
{
	// The edge currently feeding `input`, if any. An input takes a single source, so at
	// most one edge matches.
	static std::optional<Graph::Edge> findEdgeInto(const Graph& graph, PortAddress input)
	{
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to == input)
				return e;
		}
		return std::nullopt;
	}

	bool connectReplacing(Graph& graph, PortAddress from, PortAddress to)
	{
		// Free input: a plain connect suffices.
		const std::optional<Graph::Edge> existing = findEdgeInto(graph, to);
		if (!existing)
			return graph.connect(from, to) == Connection::Ok;

		// Occupied input: free it, then try the new edge. Removing an edge *into* `to`
		// can't change what `to` reaches, so the cycle verdict is the same as it would
		// have been with the old edge still present.
		graph.disconnect(to);
		if (graph.connect(from, to) == Connection::Ok)
			return true;

		// Rejected — restore the original edge so a failed replace is a no-op, not a
		// silent delete. The restore always succeeds: the input is free again, the type
		// still matches, and re-adding an edge that was acyclic before (nothing else
		// changed) stays acyclic.
		graph.connect(existing->from, to);
		return false;
	}

	bool connectReplacing(Graph& graph, NodeId from, PortIndex outPort, NodeId to, PortIndex inPort)
	{
		// Resolve the positions to stable addresses; bail (no-op) if either is out of range.
		if (!graph.contains(from) || !graph.contains(to))
			return false;
		Node& source = graph.node(from);
		Node& target = graph.node(to);
		if (outPort >= source.outputCount() || inPort >= target.inputCount())
			return false;
		return connectReplacing(graph, PortAddress{from, source.output(outPort).id()},
								PortAddress{to, target.input(inPort).id()});
	}

	bool disconnect(Graph& graph, PortAddress input)
	{
		return graph.disconnect(input);
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
			removed |= graph.disconnect(e.to);
		for (const NodeId n : nodes)
			removed |= graph.removeNode(n);
		return removed;
	}

	NodeId addNode(Graph& graph, std::unique_ptr<Node> node)
	{
		return graph.add(std::move(node));
	}

	PortId addPort(Graph& graph, NodeId node, const std::string& typeKey, std::string name)
	{
		if (!graph.contains(node))
			return PortId{};
		auto* dynamic = dynamic_cast<DynamicPortsNode*>(&graph.node(node));
		if (dynamic == nullptr)
			return PortId{};
		return addPortOfType(*dynamic, typeKey, std::move(name));
	}

	bool removePort(Graph& graph, PortAddress port)
	{
		// Collect the inputs to free first — an edge touches `port` as its source (feeding a
		// downstream input) or as its own input, and disconnecting shifts the edge list.
		std::vector<PortAddress> toDisconnect;
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.from == port || e.to == port)
				toDisconnect.push_back(e.to);
		}
		for (const PortAddress& input : toDisconnect)
			graph.disconnect(input);

		return graph.removePort(port);
	}
} // namespace lain::flow::edit
