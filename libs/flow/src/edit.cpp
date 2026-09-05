#include "lain/flow/edit.h"

#include "lain/flow/dynamicports.h"
#include "lain/flow/group.h" // GroupNode — syncGroupPorts reconciles its mirrored ports
#include "lain/flow/node.h"
#include "lain/flow/porttyperegistry.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
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
	static const Port* innerPinFor(const Graph& inner, Port::Direction outerSide, PortId pin)
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
		// Read-only: reconciliation reads the inner boundary and writes only the group's OWN outer
		// ports and the parent's edges. That is why this works on a linked group whose interior is a
		// shared definition (ADR-0013).
		const Graph& inner = *node->innerGraph();

		// The node's own interior-derived state first, before any port moves: a kind that stores
		// something about its interior which is NOT derivable from it (a loop's carry pairing) gets
		// to drop what has become false. A no-op for a group and a map, which store nothing.
		node->reconcileInterior();

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
				// ...unless another port on this side already carries that name. Only a kind with
				// ports of its OWN can produce that (a loop's `count` / `iterations`), and taking
				// the name anyway would leave two same-named outer ports — which makes every
				// name-addressed edge through them ambiguous on disk. Keep the old label and report
				// it, the same call exposePort makes when the collision arrives by the other door.
				if (node->hasPortNamed(side, pin->name()))
				{
					sync.refused.push_back(pin->name());
				}
				else
				{
					outerPort->setName(pin->name());
					++sync.renamed;
				}
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

		const GroupInputNode& boundaryIn = inner.boundaryInputNode();
		for (std::size_t i = 0; i < boundaryIn.outputCount(); ++i)
		{
			const Port& pin = boundaryIn.output(i);
			// A pin the node does not mirror at all is not a candidate and not a refusal — a
			// loop's reserved pins are the engine's, and naming them would make every loop
			// report two problems forever.
			if (mirroredInputs.count(pin.id()) == 0 && node->mirrorsPin(Port::Direction::Input, pin))
			{
				// A map refuses a pin whose type has no registered collection form, and a loop
				// one whose name collides with a port of its own — each adding nothing. So count
				// what was actually mirrored, not what was attempted, and NAME what was not,
				// since that is the part a user can act on.
				if (node->exposePort(Port::Direction::Input, pin) != PortId{})
					++sync.added;
				else
					sync.refused.push_back(pin.name());
			}
		}

		const GroupOutputNode& boundaryOut = inner.boundaryOutputNode();
		for (std::size_t i = 0; i < boundaryOut.inputCount(); ++i)
		{
			const Port& pin = boundaryOut.input(i);
			if (mirroredOutputs.count(pin.id()) == 0 && node->mirrorsPin(Port::Direction::Output, pin))
			{
				if (node->exposePort(Port::Direction::Output, pin) != PortId{})
					++sync.added;
				else
					sync.refused.push_back(pin.name());
			}
		}

		if (sync.changed())
			parent.bumpNodeVersion(group); // its interface moved — a recipe change, so every evaluation re-runs it
		return sync;
	}

	// --- Group / ungroup: moving nodes between nesting levels -------------------------------------

	// A name for a new boundary pin: the mirrored port's own name, uniquified against the pins already
	// on that side. Port names are identifiers — they double as cli flags and as the on-disk edge key —
	// so the suffix is `_2`, not " 2", which validPortName would reject.
	static std::string uniquePinName(const Node& boundary, Port::Direction side, const std::string& base)
	{
		if (!boundary.hasPortNamed(side, base))
			return base;
		for (int n = 2;; ++n)
		{
			std::string candidate = base + "_" + std::to_string(n);
			if (!boundary.hasPortNamed(side, candidate))
				return candidate;
		}
	}

	// The group's outer port mirroring inner pin `pin` on `side`. syncGroupPorts has just created
	// these, and portMap()'s VALUE is only meaningful together with a direction — a PortId is minted
	// per NODE, so the inner GroupInput's first pin and the inner GroupOutput's first pin are BOTH
	// PortId{1} (see GroupNode::portMap). Hence the side argument, and not a plain reverse lookup.
	static PortId outerPortFor(const GroupNode& group, Port::Direction side, PortId pin)
	{
		for (const auto& [outer, inner] : group.portMap())
		{
			if (inner != pin)
				continue;
			const bool isInput = group.findInput(outer) != nullptr;
			if (isInput == (side == Port::Direction::Input))
				return outer;
		}
		return PortId{};
	}

	// Position of `value` in `list` — which boundary pin a crossing edge goes through. The cut-set
	// lists are small and first-encounter ordered (a fan-out from one source must become ONE pin, and
	// edge insertion order is the only order a Graph has), so a scan is the right lookup.
	static std::size_t positionOf(const std::vector<PortAddress>& list, PortAddress value)
	{
		return static_cast<std::size_t>(std::find(list.begin(), list.end(), value) - list.begin());
	}

	// The same, adding `value` if it is not there yet — building those lists.
	static void addDistinct(std::vector<PortAddress>& list, PortAddress value)
	{
		if (std::find(list.begin(), list.end(), value) == list.end())
			list.push_back(value);
	}

	GroupResult groupSelected(Graph& parent, const std::vector<NodeId>& selection)
	{
		GroupResult result;

		// --- 0. Vet the selection, before anything moves ------------------------------------------
		// In the parent's own node order rather than the caller's: a canvas selection arrives in
		// whatever order imnodes happens to hold it, and the inner graph's node order (which is what a
		// save writes) should not depend on that.
		const std::set<NodeId> asked(selection.begin(), selection.end());
		std::vector<NodeId> inside;
		for (const NodeId id : parent.nodeIds())
		{
			if (asked.count(id) != 0)
				inside.push_back(id);
		}
		if (inside.empty())
		{
			result.refusal = GroupRefusal::EmptySelection;
			return result;
		}

		// Grouping the graph's own interface would leave the document with no way in or out — and
		// Graph::extract refuses the boundary pair anyway, so without this the gesture would strand a
		// half-built group.
		const NodeId boundaryIn = parent.boundaryInputNode().id();
		const NodeId boundaryOut = parent.boundaryOutputNode().id();
		for (const NodeId id : inside)
		{
			if (id == boundaryIn || id == boundaryOut)
			{
				result.refusal = GroupRefusal::ContainsBoundary;
				return result;
			}
		}
		const auto selected = [&inside](NodeId id)
		{ return std::find(inside.begin(), inside.end(), id) != inside.end(); };

		// --- 1. The cut-set -----------------------------------------------------------------------
		std::vector<Graph::Edge> entering, leaving, internal;
		std::vector<PortAddress> inSources, outSources; // the distinct ports each side of the cut
		for (const Graph::Edge& e : parent.edges())
		{
			const bool fromIn = selected(e.from.node);
			const bool toIn = selected(e.to.node);
			if (fromIn && toIn)
			{
				internal.push_back(e);
			}
			else if (toIn)
			{
				entering.push_back(e);
				addDistinct(inSources, e.from);
			}
			else if (fromIn)
			{
				leaving.push_back(e);
				addDistinct(outSources, e.from);
			}
		}

		// Contracting a selection into one node can create a CYCLE the original graph did not have: if
		// a value leaves the selection, passes through nodes outside it, and comes back in, the group
		// would have to run both before and after them. Graph::connect would refuse those edges one at
		// a time, leaving the group built and silently unwired — so ask the question up front, where
		// the answer is still "nothing happened".
		{
			std::vector<NodeId> stack;
			for (const Graph::Edge& e : leaving)
				stack.push_back(e.to.node);
			std::set<NodeId> visited;
			while (!stack.empty())
			{
				const NodeId at = stack.back();
				stack.pop_back();
				if (selected(at))
				{
					result.refusal = GroupRefusal::WouldCycle;
					return result;
				}
				if (!visited.insert(at).second)
					continue;
				for (const Graph::Edge& e : parent.edges())
				{
					if (e.from.node == at)
						stack.push_back(e.to.node);
				}
			}
		}

		// A crossing value needs a boundary pin, and a boundary pin is rebuilt on load through its
		// port-type KEY — so a type the registry cannot name would give a group that works now and
		// comes back after a save missing that pin and every edge through it (the serializer skips a
		// dynamic pin it cannot name). Checked for the whole cut-set before the first node moves.
		const auto pinTypeKey = [&parent](PortAddress source) -> std::string
		{
			const Port* port = parent.node(source.node).findOutput(source.port);
			return port == nullptr ? std::string{} : portTypeKey(port->type());
		};
		for (const std::vector<PortAddress>* side : {&inSources, &outSources})
		{
			for (const PortAddress& source : *side)
			{
				if (pinTypeKey(source).empty())
				{
					result.refusal = GroupRefusal::UnnamedPinType;
					return result;
				}
			}
		}

		// --- 2. The group, and the interface the cut-set implies ----------------------------------
		const NodeId groupId = parent.add(std::make_unique<InlineGroupNode>());
		auto& group = static_cast<InlineGroupNode&>(parent.node(groupId));
		Graph& inner = group.inner();
		GroupInputNode& innerIn = inner.boundaryInputNode();
		GroupOutputNode& innerOut = inner.boundaryOutputNode();
		const NodeId innerInId = innerIn.id();
		const NodeId innerOutId = innerOut.id();

		std::vector<PortId> inPins, outPins; // parallel to inSources / outSources
		for (const PortAddress& source : inSources)
		{
			const Port& port = *parent.node(source.node).findOutput(source.port);
			inPins.push_back(addPortOfType(innerIn, pinTypeKey(source),
										   uniquePinName(innerIn, Port::Direction::Output, port.name())));
			++result.inputPins;
		}
		for (const PortAddress& source : outSources)
		{
			const Port& port = *parent.node(source.node).findOutput(source.port);
			outPins.push_back(addPortOfType(innerOut, pinTypeKey(source),
											uniquePinName(innerOut, Port::Direction::Input, port.name())));
			++result.outputPins;
		}

		// --- 3. Move the nodes down ---------------------------------------------------------------
		// Their ids travel with them (Graph::extract), so every address captured above still names the
		// same port once it is inside — which is what lets step 4 replay the edge list verbatim.
		for (const NodeId id : inside)
		{
			if (inner.add(parent.extract(id), id) == id)
				result.moved.push_back(id); // record what actually landed, not what was asked for
		}

		// --- 4. Re-create the wiring inside -------------------------------------------------------
		for (const Graph::Edge& e : internal)
			inner.connect(e.from, e.to);
		for (const Graph::Edge& e : entering) // per EDGE: one pin may feed several inner consumers
			inner.connect(PortAddress{innerInId, inPins[positionOf(inSources, e.from)]}, e.to);
		for (std::size_t i = 0; i < outSources.size(); ++i) // per SOURCE: a pin is an input, single-source
			inner.connect(outSources[i], PortAddress{innerOutId, outPins[i]});

		// --- 5. Mirror the interface outward and re-attach the parent -----------------------------
		syncGroupPorts(parent, groupId);
		for (std::size_t i = 0; i < inSources.size(); ++i) // per SOURCE: an outer input is single-source
		{
			const PortId outer = outerPortFor(group, Port::Direction::Input, inPins[i]);
			if (outer != PortId{})
				parent.connect(inSources[i], PortAddress{groupId, outer});
		}
		for (const Graph::Edge& e : leaving) // per EDGE: one outer output may feed several consumers
		{
			const PortId outer = outerPortFor(group, Port::Direction::Output, outPins[positionOf(outSources, e.from)]);
			if (outer != PortId{})
				parent.connect(PortAddress{groupId, outer}, e.to);
		}

		result.group = groupId;
		return result;
	}

	UngroupResult ungroup(Graph& parent, NodeId group)
	{
		UngroupResult result;
		if (!parent.contains(group))
		{
			result.refusal = GroupRefusal::NotAGroup;
			return result;
		}
		// INLINE only. A linked group's interior is its template's definition, shared by every other
		// instance of it (ADR-0013) — there is nothing here this document owns to splice, and the type
		// hands out no mutable interior to take it from. Make it local first.
		Node& node = parent.node(group);
		auto* inlineGroup = dynamic_cast<InlineGroupNode*>(&node);
		if (inlineGroup == nullptr)
		{
			result.refusal = dynamic_cast<GroupNode*>(&node) != nullptr ? GroupRefusal::NotInline
																		: GroupRefusal::NotAGroup;
			return result;
		}
		Graph& inner = inlineGroup->inner();
		const NodeId innerInId = inner.boundaryInputNode().id();
		const NodeId innerOutId = inner.boundaryOutputNode().id();

		// --- 1. Capture every wiring fact, before anything is destroyed ---------------------------
		// What feeds each of the group's outer inputs, and what each outer output feeds. Recorded as
		// INNER pin ids, because that is the identity the two sides share (portMap), and the outer
		// ports are about to go.
		std::vector<std::pair<PortId, PortAddress>> feeding;   // inner GroupInput pin <- parent source
		std::vector<std::pair<PortId, PortAddress>> feedingTo; // inner GroupOutput pin -> parent destination
		for (const Graph::Edge& e : parent.edges())
		{
			if (e.to.node == group)
			{
				if (const PortId pin = inlineGroup->innerPin(e.to.port); pin != PortId{})
					feeding.emplace_back(pin, e.from);
			}
			else if (e.from.node == group)
			{
				if (const PortId pin = inlineGroup->innerPin(e.from.port); pin != PortId{})
					feedingTo.emplace_back(pin, e.to);
			}
		}

		// The inner half: which inner inputs each boundary-input pin drives, and which inner output
		// produces each boundary-output pin. Those are the two hops a boundary pin stands for, and
		// joining them is what "resolve each pin back to direct edges" means.
		std::vector<Graph::Edge> innerEdges;
		std::vector<std::pair<PortId, PortAddress>> driven;	  // GroupInput pin -> inner destination
		std::vector<std::pair<PortId, PortAddress>> produced; // GroupOutput pin <- inner source
		for (const Graph::Edge& e : inner.edges())
		{
			if (e.from.node == innerInId)
				driven.emplace_back(e.from.port, e.to);
			else if (e.to.node == innerOutId)
				produced.emplace_back(e.to.port, e.from);
			else
				innerEdges.push_back(e);
		}

		// --- 2. Lift the inner nodes into the parent ----------------------------------------------
		// Ids travel, as they do going down, so the captured addresses keep naming the same ports.
		std::vector<NodeId> toMove;
		for (const NodeId id : inner.nodeIds())
		{
			if (id != innerInId && id != innerOutId)
				toMove.push_back(id);
		}
		for (const NodeId id : toMove)
		{
			if (parent.add(inner.extract(id), id) == id)
				result.moved.push_back(id);
		}

		// --- 3. Drop the group, freeing the parent inputs it was feeding --------------------------
		// Before reconnecting, not after: those destination inputs are still occupied by the group's
		// own edges, and an input takes a single source.
		parent.removeNode(group);

		// --- 4. Replay the wiring, joining the two hops through each pin --------------------------
		for (const Graph::Edge& e : innerEdges)
			parent.connect(e.from, e.to);
		for (const auto& [pin, source] : feeding) // outside source -> every inner consumer of that pin
		{
			for (const auto& [drivenPin, destination] : driven)
			{
				if (drivenPin == pin && parent.connect(source, destination) == Connection::Ok)
					++result.reconnected;
			}
		}
		for (const auto& [pin, destination] : feedingTo) // inner producer -> every outside consumer
		{
			for (const auto& [producedPin, source] : produced)
			{
				if (producedPin == pin && parent.connect(source, destination) == Connection::Ok)
					++result.reconnected;
			}
		}
		return result;
	}

	ReplaceResult replaceGroup(Graph& parent, NodeId group, std::unique_ptr<GroupNode> replacement)
	{
		ReplaceResult result;
		if (replacement == nullptr || !parent.contains(group) || dynamic_cast<const GroupNode*>(&parent.node(group)) == nullptr)
		{
			result.refusal = GroupRefusal::NotAGroup;
			return result;
		}

		// Capture the parent's wiring by PORT NAME — the only thing the outgoing and incoming
		// interfaces share, since PortIds are minted per node. Both group kinds take their port names
		// from their inner boundary, so an unchanged interface comes back whole.
		const Node& old = parent.node(group);
		std::vector<std::pair<std::string, PortAddress>> fedBy;	  // outer input name <- parent source
		std::vector<std::pair<std::string, PortAddress>> feeding; // outer output name -> parent destination
		for (const Graph::Edge& e : parent.edges())
		{
			if (e.to.node == group)
			{
				if (const Port* port = old.findInput(e.to.port); port != nullptr)
					fedBy.emplace_back(port->name(), e.from);
			}
			else if (e.from.node == group)
			{
				if (const Port* port = old.findOutput(e.from.port); port != nullptr)
					feeding.emplace_back(port->name(), e.to);
			}
		}
		const std::string displayName = old.name(); // the user's title for this group, not the kind's

		// Out with the old (which drops its edges, freeing the destination inputs), in with the new AT
		// THE SAME ID — see the header: this is the same group, differently backed.
		parent.removeNode(group);
		const NodeId id = parent.add(std::move(replacement), group);
		parent.node(id).setName(displayName);
		syncGroupPorts(parent, id); // mirror the replacement's interior outward

		const Node& fresh = parent.node(id);
		const auto portNamed = [&fresh](Port::Direction side, const std::string& name) -> const Port*
		{
			const std::size_t count = (side == Port::Direction::Input) ? fresh.inputCount() : fresh.outputCount();
			for (std::size_t i = 0; i < count; ++i)
			{
				const Port& p = (side == Port::Direction::Input) ? fresh.input(i) : fresh.output(i);
				if (p.name() == name)
					return &p;
			}
			return nullptr;
		};

		for (const auto& [name, source] : fedBy)
		{
			const Port* port = portNamed(Port::Direction::Input, name);
			if (port != nullptr && parent.connect(source, PortAddress{id, port->id()}) == Connection::Ok)
				++result.reconnected;
			else
				++result.dropped;
		}
		for (const auto& [name, destination] : feeding)
		{
			const Port* port = portNamed(Port::Direction::Output, name);
			if (port != nullptr && parent.connect(PortAddress{id, port->id()}, destination) == Connection::Ok)
				++result.reconnected;
			else
				++result.dropped;
		}
		return result;
	}
} // namespace lain::flow::edit
