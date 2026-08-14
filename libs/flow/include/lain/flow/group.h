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
#include <memory>
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
		//
		// VIRTUAL because a map mirrors the same pins LIFTED (T becomes vector<T>) — so one
		// reconciliation gesture serves both kinds and edit::syncGroupPorts needs no idea which it
		// is holding. Returns the null PortId if the pin cannot be mirrored.
		virtual PortId exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence = Presence::Required)
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

	// A MAP: a group whose interior is evaluated once per ELEMENT of a collection, rather than once
	// (ADR-0014). It owns its inner Graph exactly as an inline group does — a linked map (one shared
	// template mapped over N streams) is deferred until the video workload asks for it.
	//
	// The difference is entirely in the FACE it presents. Where a group mirrors an inner pin of type
	// T as a port of type T, a map mirrors it LIFTED, as vector<T>: its `files` input takes the whole
	// collection and its `out` output delivers one per element. The scheduler then reads each outer
	// port's declared type to decide what to do with it, so nothing about the mode is stored:
	//
	//     outer vector<T> against an inner T  -> SPLIT     (element i goes to child i)
	//     outer T         against an inner T  -> BROADCAST (the same value goes to every child)
	//     outputs                             -> GATHER    (always)
	//
	// Broadcast is therefore expressed by declaring the port un-lifted, which edit::syncGroupPorts
	// preserves: mirroring is by PortId, so an already-mapped pin is never re-typed.
	class MapNode : public GroupNode
	{
	public:
		MapNode()
			: GroupNode("Map")
		{
		}

		Graph& inner() { return m_inner; }
		const Graph& inner() const { return m_inner; }
		const Graph* innerGraph() const override { return &m_inner; }

		// The structural fact that makes this a map: its interior is evaluated once per element,
		// so it has N child evaluations rather than one, sized between stages.
		bool evaluatesPerElement() const override { return true; }

		// Mirror an inner pin LIFTED — an inner T becomes an outer vector<T>, which is what makes
		// this node a map rather than a group.
		//
		// Returns the null PortId when T has no registered list form, and adds nothing: a map cannot
		// carry a type it has no collection for, and inventing one at runtime is impossible (naming
		// std::vector<T> needs T at compile time). Refusing is the same call the encoders and
		// edit::groupSelected's UnnamedPinType make — degrade nothing, leave it visibly absent.
		PortId exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence = Presence::Required) override;

	private:
		Graph m_inner; // born with its own boundary pair — the interface each element is evaluated over
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
			, m_inner(std::make_shared<const Graph>()) // never null: every group HAS an interior
		{
		}

		const Graph* innerGraph() const override { return m_inner.get(); }

		// The definition itself, for a caller that needs to SHARE it rather than read it — the loader
		// handing one template to a second instance, and a test asserting that two instances really do
		// hold one graph rather than two equal ones.
		const std::shared_ptr<const Graph>& definition() const { return m_inner; }

		// Establish this group's interior — what a LOADER does once, not an edit. There is deliberately
		// no way to reach in and change the one already there: a definition may back N instances
		// (ADR-0013), so a mutation through any of them would reach all of them.
		void adoptInterior(std::shared_ptr<const Graph> interior)
		{
			if (interior) // a null would break "a group always has an interior" for every reader
				m_inner = std::move(interior);
		}

		// The same, for an interior this group does NOT share: an unresolved placeholder, or a
		// template loaded with no cache in play. It becomes shareable simply by being held this way.
		void adoptInterior(Graph interior) { m_inner = std::make_shared<const Graph>(std::move(interior)); }

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
		// SHARED: one definition backs every instance of a template (ADR-0013). Const, so the sharing
		// needs no lock — a template edit REPLACES a definition rather than writing into one, which is
		// what keeps ADR-0012's "a const Graph& is concurrently readable" true here.
		std::shared_ptr<const Graph> m_inner;
		std::string m_source;
		std::vector<PinSpec> m_cachedInputs;
		std::vector<PinSpec> m_cachedOutputs;
		bool m_resolved = false;
	};
} // namespace lain::flow
