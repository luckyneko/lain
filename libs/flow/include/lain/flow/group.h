#pragma once

// Group nodes — a node that CONTAINS a graph and exposes selected inner ports as its own, via the
// *same* boundary mechanism the top level uses (the top-level Graph is the outermost group).
//
//   GroupNode        — the ABSTRACT base: the port mirroring, and the structural question of which
//                      graph is inside. Says nothing about where that graph comes from or who owns it.
//   InlineGroupNode  — INLINE: the recipe is saved inside the parent document, and it owns its Graph.
//   LinkedGroupNode  — LINKED: the recipe lives in its own document (its TEMPLATE), referenced by
//                      path, read-only in place.
//   MapNode          — a group whose interior is evaluated once per ELEMENT of a collection, so its
//                      face is LIFTED (an inner T becomes an outer vector<T>). Owns its Graph.
//   LoopNode         — a group whose interior is evaluated once per ITERATION, each pass's CARRIED
//                      outputs seeding the next one's inputs. Bounded by construction. Owns its Graph.
//
// Read-only-in-place is a property of the TYPE, not a rule each caller remembers: `Node::innerGraph()`
// is const for everyone, and a kind hands out a mutable interior only if that interior is its own —
// which `editableInner()` answers, so a host never tests for a concrete class. That matters because a
// linked group's interior is SHARED between instances (M7 / ADR-0013), where a stray mutation through
// one instance would reach all of them.
//
// See ADR-0009 (the scheduler flattens all nesting into one execution plan, so compute() here is
// never called), ADR-0010 (inline vs linked, the interface cache, why overrides are deferred) and
// ADR-0013 (shared template definitions) and ADR-0021 (loop nodes).

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
		// belongs only to the kinds that OWN one.
		const Graph* innerGraph() const override = 0;

		// The contained graph as something a host may EDIT, or nullptr when this kind's interior is
		// not its own to hand out — which is exactly the linked case, whose recipe belongs to its
		// template and is shared with every other instance (ADR-0013).
		//
		// A virtual rather than a `dynamic_cast<InlineGroupNode*>` at each call site, for the reason
		// ADR-0009 gives about innerGraph(): a host asking "may I edit through this?" wants the FACT,
		// not the class. When the map arrived it was a second kind that owns its interior, and every
		// site that had cast for the class silently answered "read-only" for it — a map you could
		// descend into and never build. The next such kind needs no host change.
		virtual Graph* editableInner() { return nullptr; }

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

		// Whether this node mirrors `innerPin` outward at all. Default TRUE: a group's face IS its
		// interior's, and a map's is that face lifted, so every inner pin is a candidate.
		//
		// Separate from exposePort REFUSING one, and the distinction is not academic: a refusal is
		// something a user can act on and a host should say out loud, while a pin that is not a
		// candidate is not mirrored BY DESIGN and reporting it would mean every loop permanently
		// naming its two reserved pins as problems. This is the question, and exposePort answers the
		// other one.
		virtual bool mirrorsPin(Port::Direction /*outerSide*/, const Port& /*innerPin*/) const { return true; }

		// Reconcile whatever state this node stores ABOUT its interior against the interior as it now
		// is. Called by edit::syncGroupPorts before it touches any port, so a kind that derives its
		// whole face from the inner boundary (a group, a map) needs nothing here — hence the no-op
		// default.
		//
		// A LOOP stores a carry PAIRING, which is the one thing about an interior that is not
		// derivable from it, and a user may delete either half through the Interface pane. A virtual
		// rather than a branch in edit.cpp, for the reason exposePort is one: the reconciliation
		// gesture must not learn which kind it is holding.
		virtual void reconcileInterior() {}

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
		Graph* editableInner() override { return &m_inner; }

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

		// A map OWNS its interior, like an inline group — so a host may edit through it. (A linked
		// map, whose body would come from a shared template, is deferred; see the class header.)
		Graph* editableInner() override { return &m_inner; }

		// The structural fact that makes this a map: its interior is evaluated once per element,
		// so it has N child evaluations rather than one, sized between stages.
		InteriorEvaluation interiorEvaluation() const override { return InteriorEvaluation::PerElement; }

		// Mirror an inner pin LIFTED — an inner T becomes an outer vector<T>, which is what makes
		// this node a map rather than a group.
		//
		// Returns the null PortId when T has no registered list form, and adds nothing: a map cannot
		// carry a type it has no collection for, and inventing one at runtime is impossible (naming
		// std::vector<T> needs T at compile time). Refusing is the same call the encoders and
		// edit::groupSelected's UnnamedPinType make — degrade nothing, leave it visibly absent.
		PortId exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence = Presence::Required) override;

		// Mirror an inner pin UN-LIFTED — same type in, same type out — so the one value reaches
		// every element instead of being split across them. This is the BROADCAST half of the choice
		// ADR-0014 encodes in the port's own declared type, and the reason it needs a name of its own
		// is that it must be reachable deliberately: the loader restoring a stored interface, and a
		// host's "broadcast this pin" gesture, both ask for exactly this.
		PortId exposeBroadcast(Port::Direction outerSide, const Port& innerPin, Presence presence = Presence::Required)
		{
			return GroupNode::exposePort(outerSide, innerPin, presence);
		}

	private:
		Graph m_inner; // born with its own boundary pair — the interface each element is evaluated over
	};

	// A LOOP: a group whose interior is evaluated once per ITERATION, each pass's CARRIED outputs
	// seeding the next one's inputs (ADR-0021). Where a map's children are independent by
	// construction, a loop's are sequentially dependent — which is what makes it a different
	// execution shape rather than a variant of the map. It owns its inner Graph as an inline group
	// does; a LINKED loop is deferred exactly as a linked map is.
	//
	// Unlike a map, its FACE is derived exactly as a plain group's is — same type in, same type out.
	// The pairing changes what the engine does BETWEEN iterations, never what the ports look like:
	//
	//     a CARRIED inner pin pair  -> an outer input holding the SEED + an outer output holding the
	//                                  FINAL value (the same name on both sides, by design)
	//     an unpaired inner input   -> an outer input, INVARIANT across iterations
	//     an unpaired inner output  -> an outer output, the LAST iteration's value
	//     a RESERVED inner pin      -> nothing: it is the engine's, not the parent graph's
	//
	// So the whole difference between a loop's face and a group's is two negatives, and they are
	// deliberately different questions: a reserved pin is NOT A CANDIDATE (mirrorsPin), while a pin
	// whose name collides with one of this node's own is a candidate that is REFUSED (exposePort) —
	// the first is by design and silent, the second is something a user can act on and a host says.
	//
	// It also owns two ports of its own, which no other group kind does: `count` (the bound, with a
	// default, so an unbounded loop has no representation at all) and `iterations` (how many actually
	// ran, which is what makes "converged at 7" distinguishable from "hit the bound at 100"). They
	// mirror nothing, so portMap() never names them and edit::syncGroupPorts's removal phase is
	// already blind to them. They are declared in the constructor, so they are always this node's
	// FIRST input and FIRST output — mirrored ports are appended at reconciliation time.
	class LoopNode : public GroupNode
	{
	public:
		LoopNode();

		Graph& inner() { return m_inner; }
		const Graph& inner() const { return m_inner; }
		const Graph* innerGraph() const override { return &m_inner; }

		// A loop OWNS its interior, like an inline group and a map — so a host may edit through it.
		// Without this every pane is read-only inside a loop: one you could add, descend into, and
		// never build (M8 slice 6c, which is where that bug was found in map-shaped form).
		Graph* editableInner() override { return &m_inner; }

		// The structural fact that makes this a loop: ONE child evaluation, re-run per iteration,
		// each pass seeding the next. A map's elements are co-equal results; a loop's iterations are
		// steps toward one, so only the last survives (ADR-0021).
		InteriorEvaluation interiorEvaluation() const override { return InteriorEvaluation::PerIteration; }

		// This node's own ports (see the class header) — the trip bound it reads, and the count it
		// reports. Both int: `int` is the type a graph can actually drive (a registered port type
		// with a cli binder, a param editor and a constant node), and the two must agree.
		PortId countPort() const { return m_count; }
		PortId iterationsPort() const { return m_iterations; }

		// The RESERVED inner pins — `index` on the interior's GroupInputNode (which iteration this
		// is) and `continue` on its GroupOutputNode (whether to run another). The engine writes the
		// first and reads the second; neither is mirrored outward.
		//
		// They live on DIFFERENT nodes, so a PortId alone does not name one: a PortId is minted per
		// node, so both are PortId{1} (the constructor declares them before anything else can). Every
		// reader pairs the id with a direction, exactly as portMap()'s values are read.
		PortId indexPin() const { return m_index; }
		PortId continuePin() const { return m_continue; }

		// One carry, as the pair of inner boundary pins it IS: the value delivered on `innerOut` at
		// iteration k is what arrives on `innerIn` at iteration k+1.
		struct Carry
		{
			PortId innerIn;	 // a pin on the interior's GroupInputNode  (the body READS it)
			PortId innerOut; // a pin on the interior's GroupOutputNode (the body WRITES it)
		};

		// Add a carry: one pin of type T on EACH inner boundary node, both named `name`, recorded as
		// a pair. The paired gesture is the only way to make one, which is what stops half a carry
		// from being authored — a lone pin is not a weaker carry, it is a silent invariant (or a
		// last-iteration output) the user never asked for.
		//
		// Refuses (returning a null Carry, having added NOTHING on either side) an invalid name or
		// one already taken on either inner boundary node.
		template <typename T>
		Carry addCarry(std::string name)
		{
			GroupInputNode& into = m_inner.boundaryInputNode();
			GroupOutputNode& from = m_inner.boundaryOutputNode();

			// Both sides vetted BEFORE either is touched, so the common refusal needs no undo at all.
			if (!validPortName(name) || into.hasPortNamed(Port::Direction::Output, name) || from.hasPortNamed(Port::Direction::Input, name))
				return Carry{};

			const PortId innerIn = into.addBoundary<T>(name);
			if (innerIn == PortId{})
				return Carry{};
			const PortId innerOut = from.addBoundary<T>(name);
			if (innerOut == PortId{})
			{
				// Unreachable after the checks above — but the undo is what makes atomicity a
				// property of the code rather than of an argument about it. Nothing can be wired to
				// a pin this new, so the refusing primitive accepts the removal.
				m_inner.removePort(PortAddress{into.id(), innerIn});
				return Carry{};
			}
			m_carries[innerIn] = innerOut;
			return Carry{innerIn, innerOut};
		}

		// The whole pairing, inner GroupInput pin -> inner GroupOutput pin. Id-keyed for the reason
		// portMap() is: a rename must move a label, never behaviour. Pairing by NAME would let
		// renaming one half silently stop a loop carrying — no error, no empty value, just a
		// different answer, which is strictly worse than the wiring loss ids already prevent.
		const std::map<PortId, PortId>& carries() const { return m_carries; }

		// The reserved pin for `outerSide` is not a candidate for mirroring at all — the answer to
		// "does the add phase skip this pin?", by ID and never by name, so renaming `index` cannot
		// quietly turn a while loop into a count loop.
		bool mirrorsPin(Port::Direction outerSide, const Port& innerPin) const override;

		// Mirror an inner pin — same type, same name, as a plain group does — except that a loop is
		// the first kind that can REFUSE one: see the class header. Returns the null PortId then,
		// adding nothing, which is MapNode::exposePort's call for an unliftable type.
		PortId exposePort(Port::Direction outerSide, const Port& innerPin, Presence presence = Presence::Required) override;

		// Drop any carry whose inner pin has gone (a user removing one half through the Interface
		// pane's ±). The survivor then means exactly what an unpaired pin means — an invariant, or a
		// last-iteration output — so derivation stays total and there is no broken state to
		// represent. Called by edit::syncGroupPorts before it touches a port.
		void reconcileInterior() override;

	private:
		Graph m_inner;						// born with its own boundary pair — the loop's interface
		std::map<PortId, PortId> m_carries; // innerIn -> innerOut
		PortId m_count;						// this node's input: the trip bound
		PortId m_iterations;				// this node's output: how many ran
		PortId m_index;						// reserved, on m_inner's GroupInputNode
		PortId m_continue;					// reserved, on m_inner's GroupOutputNode
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
