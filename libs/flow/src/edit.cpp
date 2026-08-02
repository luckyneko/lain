#include "lain/flow/edit.h"

#include "lain/flow/dynamicports.h"
#include "lain/flow/group.h" // GroupNode — syncGroupPorts reconciles its mirrored ports
#include "lain/flow/node.h"
#include "lain/flow/porttyperegistry.h"

#include <optional>
#include <set>
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

	bool connectReplacing(Graph& graph, NodeId from, std::size_t outPort, NodeId to, std::size_t inPort)
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

	bool disconnect(Graph& graph, NodeId to, std::size_t inPort)
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

	// How many parent edges touch `port` — what a removal is about to cut. Reported so a host can
	// tell the user which wiring a vanished pin took with it, rather than silently dropping it.
	static int edgeCount(const Graph& graph, PortAddress port)
	{
		int count = 0;
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.from == port || e.to == port)
				++count;
		}
		return count;
	}

	// The inner boundary pin a group's outer port should mirror, or nullptr if that pin is gone.
	// Direction-scoped: an outer INPUT mirrors a pin on the inner GroupInput (whose pins are its
	// outputs), an outer OUTPUT a pin on the inner GroupOutput.
	static const Port* innerPinFor(Graph& inner, Port::Direction outerSide, PortId pin)
	{
		return (outerSide == Port::Direction::Input) ? inner.boundaryInputNode().findOutput(pin)
													 : inner.boundaryOutputNode().findInput(pin);
	}

	GroupSync syncGroupPorts(Graph& parent, NodeId group)
	{
		GroupSync sync;
		if (!parent.contains(group))
			return sync;
		auto* node = dynamic_cast<GroupNode*>(&parent.node(group));
		if (node == nullptr)
			return sync; // not a group — nothing to reconcile
		Graph& inner = node->inner();

		// --- 1. Remove outer ports whose inner pin is gone -------------------------------------
		// Collected first: removing mutates the port list, and a dropped port may still be wired in
		// the parent, so each removal goes through edit::removePort (disconnect, then the primitive).
		std::vector<PortAddress> stale;
		for (const auto& [outer, innerId] : node->portMap())
		{
			const Port* outerPort = node->findInput(outer);
			Port::Direction side = Port::Direction::Input;
			if (outerPort == nullptr)
			{
				outerPort = node->findOutput(outer);
				side = Port::Direction::Output;
			}
			if (outerPort == nullptr)
			{
				stale.push_back(PortAddress{group, outer}); // mapped, but the port itself is gone
				continue;
			}
			if (innerPinFor(inner, side, innerId) == nullptr)
				stale.push_back(PortAddress{group, outer});
		}
		for (const PortAddress& address : stale)
		{
			sync.disconnected += edgeCount(parent, address);
			if (removePort(parent, address))
				++sync.removed;
			node->unmapPort(address.port);
		}

		// --- 2. Retitle surviving ports to match their (possibly renamed) inner pin -------------
		// A rename is display-only on both sides: the mapping is by PortId, so the wiring is
		// untouched and only the label moves.
		for (const auto& [outer, innerId] : node->portMap())
		{
			Port* outerPort = node->findInput(outer);
			Port::Direction side = Port::Direction::Input;
			if (outerPort == nullptr)
			{
				outerPort = node->findOutput(outer);
				side = Port::Direction::Output;
			}
			const Port* pin = outerPort ? innerPinFor(inner, side, innerId) : nullptr;
			if (pin != nullptr && pin->name() != outerPort->name())
			{
				outerPort->setName(pin->name());
				++sync.renamed;
			}
		}

		// --- 3. Add an outer port for every inner pin not yet mirrored --------------------------
		// Last, so a name freed by a removal or a rename above is available here.
		//
		// The "already mirrored" sets are PER DIRECTION, and that is not a detail: a PortId is minted
		// per NODE, so the inner GroupInput's first pin and the inner GroupOutput's first pin are BOTH
		// PortId{1}. One shared set would let an input's mapping mask the same-numbered output pin, so
		// the output port would silently never be created — which is exactly what a single set did.
		std::set<PortId> mirroredInputs;
		std::set<PortId> mirroredOutputs;
		for (const auto& [outer, innerId] : node->portMap())
		{
			if (node->findInput(outer) != nullptr)
				mirroredInputs.insert(innerId);
			else if (node->findOutput(outer) != nullptr)
				mirroredOutputs.insert(innerId);
		}

		GroupInputNode& boundaryIn = inner.boundaryInputNode();
		for (std::size_t i = 0; i < boundaryIn.outputCount(); ++i)
		{
			const Port& pin = boundaryIn.output(i);
			if (mirroredInputs.count(pin.id()) == 0)
			{
				node->exposePort(Port::Direction::Input, pin);
				++sync.added;
			}
		}

		GroupOutputNode& boundaryOut = inner.boundaryOutputNode();
		for (std::size_t i = 0; i < boundaryOut.inputCount(); ++i)
		{
			const Port& pin = boundaryOut.input(i);
			if (mirroredOutputs.count(pin.id()) == 0)
			{
				node->exposePort(Port::Direction::Output, pin);
				++sync.added;
			}
		}

		if (sync.changed())
			parent.node(group).markDirty(); // its interface moved — re-run it
		return sync;
	}
} // namespace lain::flow::edit
