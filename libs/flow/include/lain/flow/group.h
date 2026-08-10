#pragma once

// Group nodes — a node that CONTAINS a graph and exposes selected inner ports as its own, via the
// *same* boundary mechanism the top level uses (the top-level Graph is the outermost group).
//
//   GroupNode        — the ABSTRACT base: the port mirroring, and the structural question of which
//                      graph is inside. Says nothing about where that graph comes from or who owns it.
//   InlineGroupNode  — INLINE: the recipe is saved inside the parent document. It owns its Graph, and
//                      is the only kind with MUTABLE access to an interior (`inner()`).
//   LinkedGroupNode  — LINKED: the recipe lives in its own document (its TEMPLATE), referenced by
//                      path, read-only in place.
//
// Read-only-in-place is a property of the TYPE, not a rule each caller remembers: `Node::innerGraph()`
// is const for everyone, and only the inline kind hands out a mutable interior. That matters because
// a linked group's interior is about to be SHARED between instances (M7 / ADR-0013), where a stray
// mutation through one instance would reach all of them.
//
// See ADR-0009 (the scheduler flattens all nesting into one execution plan, so compute() here is
// never called), ADR-0010 (inline vs linked, the interface cache, why overrides are deferred) and
// ADR-0013 (shared template definitions).

#include "lain/flow/graph.h"
#include "lain/flow/node.h"
#include "lain/flow/types.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lain::flow
{
	// The shared half of a group node: its ports MIRROR the boundary pins of whatever graph it
	// contains — this node's inputs are the inner GroupInputNode's output pins, its outputs the inner
	// GroupOutputNode's input pins. Abstract, because where the contained graph comes from is exactly
	// what distinguishes the two kinds.
	class GroupNode : public Node
	{
	public:
		// The graph this node contains — CONST for everyone (see the file header). A mutable interior
		// is InlineGroupNode's alone.
		const Graph* innerGraph() const override = 0;

		// (There is no `dirty()` override any more. A group used to report itself dirty when anything
		// inside it was — a walk over mutable state on the inner definition. Staleness is now a
		// per-node version compared against ONE evaluation's record, so "is anything inside stale?"
		// is a question about the group's CHILD Evaluation, and the scheduler asks it there.)

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
			const PortId outer = addInput<T>(std::move(name), presence);
			mapPort(outer, innerPin);
			return outer;
		}

		template <typename T>
		PortId exposeOutput(std::string name, PortId innerPin)
		{
			const PortId outer = addOutput<T>(std::move(name));
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
									 ? addInputLike(innerPin.name(), innerPin.portType(), presence)
									 : addOutputLike(innerPin.name(), innerPin.portType());
			mapPort(outer, innerPin.id());
			return outer;
		}

		// Never called: the scheduler expands a group into its inner steps plus an entry/exit step
		// pair, so a group is never executed AS a node (ADR-0009). Kept as a no-op rather than an
		// assert because a group is a perfectly valid inert node outside a scheduler.
		void compute(NodeEvaluation&) const override {}

	protected:
		explicit GroupNode(std::string name)
			: Node(std::move(name))
		{
		}

	private:
		std::map<PortId, PortId> m_outerToInner;
	};

	// An INLINE group: the recipe is stored inside the parent document, so this node OWNS its inner
	// graph and is the only group kind that hands out a mutable one. Its edits mark the parent dirty
	// and ride the parent's undo history, because an inline subgraph is literally part of the parent
	// document the undo snapshot already captures.
	class InlineGroupNode : public GroupNode
	{
	public:
		InlineGroupNode()
			: GroupNode("Group")
		{
		}

		// The mutable interior — the thing that makes this kind different. The loader builds into it,
		// and the host edits through it.
		Graph& inner() { return m_inner; }
		const Graph& inner() const { return m_inner; }

		const Graph* innerGraph() const override { return &m_inner; }

	private:
		Graph m_inner; // born with its own boundary pair — the group's interface
	};

	// One pin of a linked group's cached interface: enough to rebuild the port without the template.
	// `typeKey` is a port-type registry key (registerPortType<T>), which is already core — so an
	// unresolved link builds REAL placeholder pins, not untyped stubs.
	struct PinSpec
	{
		std::string name;
		std::string typeKey;
	};

	// A LINKED group: the same port mirroring, plus where the recipe came from. Read-only in place —
	// and now by CONSTRUCTION rather than by the host remembering: there is no mutable accessor to
	// this node's interior at all. A template edit changes every linked group built from it, so it
	// must be an explicit act (Edit Template…), not a side effect of clicking in.
	class LinkedGroupNode : public GroupNode
	{
	public:
		LinkedGroupNode()
			: GroupNode("Linked Group")
		{
		}

		const Graph* innerGraph() const override { return &m_inner; }

		// Establish this group's interior — what a LOADER does once, not an edit. A whole graph moves
		// in; there is deliberately no way to reach in and change the one that is already there.
		// (M7 slice 2 replaces the owned Graph with a shared_ptr<const Graph> from the template cache;
		// this is the seam that absorbs that, so callers do not change again.)
		void adoptInterior(Graph interior) { m_inner = std::move(interior); }

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
		// Owned for now. M7 slice 2 makes this a shared_ptr<const Graph> pointing at the template
		// cache, so N instances of one template share one definition (ADR-0013).
		Graph m_inner;
		std::string m_source;
		std::vector<PinSpec> m_cachedInputs;
		std::vector<PinSpec> m_cachedOutputs;
		bool m_resolved = false;
	};
} // namespace lain::flow
