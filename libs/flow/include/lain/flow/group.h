#pragma once

// Group nodes — a node that CONTAINS a graph and exposes selected inner ports as its own, via the
// *same* boundary mechanism the top level uses (the top-level Graph is the outermost group). Two
// kinds, differing only in where the recipe is stored and whether it may be edited in place:
//
//   GroupNode        — INLINE: the recipe is saved inside the parent document, editable in place.
//   LinkedGroupNode  — LINKED: the recipe lives in its own document (its TEMPLATE), referenced by
//                      path, read-only in place. A link references a RECIPE, never a running graph
//                      — every group owns its own inner Graph, because a Port holds a persistent
//                      value and sharing one would make two instances stomp each other's
//                      intermediates (concurrently, under the flattened plan).
//
// See ADR-0009 (the scheduler flattens all nesting into one execution plan, so compute() here is
// never called) and ADR-0010 (inline vs linked, the interface cache, why overrides are deferred).

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/types.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lain::flow
{
	// An INLINE group: owns its inner graph, and its own ports mirror that graph's boundary pins —
	// this node's inputs are the inner GroupInputNode's output pins, its outputs the inner
	// GroupOutputNode's input pins.
	class GroupNode : public Node
	{
	public:
		GroupNode()
			: GroupNode("Group")
		{
		}

		Graph& inner() { return m_inner; }
		const Graph& inner() const { return m_inner; }

		// The scheduler's structural question (see Node::innerGraph): yes, expand me.
		Graph* innerGraph() override { return &m_inner; }

		// Dirty when this node is, OR when anything inside it is. The inner walk recurses naturally
		// through nested groups, since an inner group answers this same way.
		bool dirty() const override
		{
			if (Node::dirty())
				return true;
			for (const NodeId id : m_inner.nodeIds())
			{
				if (m_inner.node(id).dirty())
					return true;
			}
			return false;
		}

		// The inner boundary pin an outer port mirrors, or the null PortId if unmapped. Identity is
		// the PortId pair, NOT the name: renaming an inner pin must keep the outer wiring, which is
		// the whole reason ports carry stable ids. edit::syncGroupPorts maintains this mapping.
		PortId innerPin(PortId outer) const override
		{
			const auto it = m_outerToInner.find(outer);
			return it == m_outerToInner.end() ? PortId{} : it->second;
		}
		void mapPort(PortId outer, PortId inner) { m_outerToInner[outer] = inner; }
		void unmapPort(PortId outer) { m_outerToInner.erase(outer); }

		// The whole mapping. NOTE the asymmetry: the KEY is unique (an outer PortId is unique across
		// both of this node's sides), but the VALUE is not — a PortId is minted per NODE, so the
		// inner GroupInput's first pin and the inner GroupOutput's first pin are BOTH PortId{1}.
		// A value is therefore only meaningful together with the outer port's DIRECTION, which says
		// which boundary node to resolve it against: an outer input against the GroupInput, an outer
		// output against the GroupOutput. Every reader here does that (the scheduler's entry/exit
		// steps, edit::syncGroupPorts), and a test pins it down by wiring an inner graph crossed.
		const std::map<PortId, PortId>& portMap() const { return m_outerToInner; }

		// Expose an inner boundary pin as one of this node's own ports, recording the mapping —
		// the PRIMITIVE the mirroring is built from. edit::syncGroupPorts is the GESTURE over it:
		// it reconciles the whole port set against the inner interface and disconnects the parent's
		// edges for pins that vanished (which needs the parent Graph, so it cannot live here).
		// `presence` matches an ordinary input's, so a group can hold optional inputs like any node.
		template <typename T>
		PortId exposeInput(std::string name, PortId innerPin, Presence presence = Presence::Required)
		{
			const PortIndex index = addInput<T>(std::move(name), presence);
			const PortId outer = input(index).id();
			mapPort(outer, innerPin);
			return outer;
		}

		template <typename T>
		PortId exposeOutput(std::string name, PortId innerPin)
		{
			const PortIndex index = addOutput<T>(std::move(name));
			const PortId outer = output(index).id();
			mapPort(outer, innerPin);
			return outer;
		}

		// Mirror an inner boundary pin as one of this node's own ports — same type, same name — with
		// no compile-time T, since the pin already carries the PortType flyweight. This is what
		// edit::syncGroupPorts calls: reconciliation runs over a live interface whose types are only
		// known at runtime. `outerSide` is THIS node's side: an inner GroupInput pin (an inner
		// *output*) becomes one of this node's inputs, and vice versa.
		PortId exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence = Presence::Required)
		{
			const PortId outer = (outerSide == Port::Direction::Input)
									 ? input(addInputLike(innerPin.name(), innerPin.portType(), presence)).id()
									 : output(addOutputLike(innerPin.name(), innerPin.portType())).id();
			mapPort(outer, innerPin.id());
			return outer;
		}

		// Never called: the scheduler expands a group into its inner steps plus an entry/exit step
		// pair, so a group is never executed AS a node (ADR-0009). Kept as a no-op rather than an
		// assert because a group is a perfectly valid inert node outside a scheduler.
		void compute() override {}

	protected:
		explicit GroupNode(std::string name)
			: Node(std::move(name))
		{
		}

	private:
		Graph m_inner; // born with its own boundary pair — the group's interface
		std::map<PortId, PortId> m_outerToInner;
	};

	// One pin of a linked group's cached interface: enough to rebuild the port without the template.
	// `typeKey` is a port-type registry key (registerPortType<T>), which is already core — so an
	// unresolved link builds REAL placeholder pins, not untyped stubs.
	struct PinSpec
	{
		std::string name;
		std::string typeKey;
	};

	// A LINKED group: the same inner graph and port mirroring, plus where the recipe came from.
	// Read-only in place — enforced by the editing layer / host, since a template edit changes every
	// linked group built from it and so must be an explicit act, not a side effect of clicking in.
	class LinkedGroupNode : public GroupNode
	{
	public:
		LinkedGroupNode()
			: GroupNode("Linked Group")
		{
		}

		// The template's path AS WRITTEN in the parent document — relative to that document, so a
		// project folder stays portable. Resolving it to a document is the host's job (flow core
		// does no file I/O); the serializer takes an injected resolver.
		const std::string& source() const { return m_source; }
		void setSource(std::string source) { m_source = std::move(source); }

		// The interface cache: the pins this group had when the PARENT was last saved. Subordinate
		// to the template — which always wins when present — it exists so that a MISSING template
		// degrades to a repairable placeholder (correct pins, wiring intact, lossless to save)
		// instead of dropping every edge into and out of this node, and so a CHANGED interface can
		// be diffed and reported rather than silently losing edges.
		const std::vector<PinSpec>& cachedInputs() const { return m_cachedInputs; }
		const std::vector<PinSpec>& cachedOutputs() const { return m_cachedOutputs; }
		void setCachedInterface(std::vector<PinSpec> inputs, std::vector<PinSpec> outputs)
		{
			m_cachedInputs = std::move(inputs);
			m_cachedOutputs = std::move(outputs);
		}

		// Whether the template was found and loaded. False means this is a placeholder standing in
		// for an unresolved link: its ports come from the cache, and the host marks it broken.
		bool resolved() const { return m_resolved; }
		void setResolved(bool resolved) { m_resolved = resolved; }

	private:
		std::string m_source;
		std::vector<PinSpec> m_cachedInputs;
		std::vector<PinSpec> m_cachedOutputs;
		bool m_resolved = false;
	};
} // namespace lain::flow
