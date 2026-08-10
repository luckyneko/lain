#pragma once

#include "lain/flow/param.h"
#include "lain/flow/port.h"
#include "lain/flow/types.h"

#include <cstdint>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace lain::flow
{
	class Graph;		  // a node may CONTAIN one (see innerGraph) — the group-node seam
	class NodeEvaluation; // the per-node runtime view compute() is handed (evaluation.h)

	// Abstract base for a graph node — a DEFINITION: what it is, what ports and params it declares,
	// what it computes. It holds no runtime state at all; values and bookkeeping live in an
	// Evaluation (ADR-0012).
	//
	// Threading contract: compute() is CONST and reads/writes only through the NodeEvaluation it is
	// given — its own inputs and its own outputs. That is what lets the scheduler run independent
	// nodes on different threads, and what lets one definition be run by several evaluations at once
	// (N video streams through one subgraph). An on-request source (a camera capture) rearms itself
	// with `evaluation.requestRecompute()`, which demands work of THAT evaluation and says nothing
	// about the recipe.
	class Node
	{
	public:
		virtual ~Node() = default;

		NodeId id() const { return m_id; }
		const std::string& name() const { return m_name; }

		// Rename the node — display only, like Port::setName. A node's identity is its NodeId: edges,
		// handles and the editor's layout all reference that, and nothing addresses a node by name (a
		// name need not even be unique — an adapter that wants uniqueness imposes it). Serialization
		// persists the name, so a user-chosen title survives a save/load round-trip.
		void setName(std::string name) { m_name = std::move(name); }

		std::size_t inputCount() const { return m_inputs.size(); }
		std::size_t outputCount() const { return m_outputs.size(); }

		// By POSITION — for deliberate ordered iteration (walking every pin in declaration order:
		// the inspector, serialization, a boundary pin list). A position is a cursor, not a
		// reference: it moves when sibling ports are added or removed, so it is never stored.
		Port& input(std::size_t i) { return m_inputs[i]; }
		const Port& input(std::size_t i) const { return m_inputs[i]; }
		Port& output(std::size_t i) { return m_outputs[i]; }
		const Port& output(std::size_t i) const { return m_outputs[i]; }

		// By IDENTITY — how a node reaches its OWN ports, using the ids its declarations returned
		// (`output(m_result).set(...)`). Unchecked, like the index accessors: a node's named PortId
		// always names a port of that node, so a miss is a programming error, caught by assert in
		// debug. Reach for findInput / findOutput when the id came from elsewhere and may be stale.
		//
		// PortId is a distinct type rather than an integer, so these never collide with the index
		// overloads above.
		Port& input(PortId id) { return *checked(findInput(id)); }
		const Port& input(PortId id) const { return *checked(findInput(id)); }
		Port& output(PortId id) { return *checked(findOutput(id)); }
		const Port& output(PortId id) const { return *checked(findOutput(id)); }

		// Resolve a port by its stable PortId (what edges reference), or nullptr if it is not
		// found — a linear scan (a node has few ports). Direction-scoped, so an edge's `from`
		// resolves through findOutput and its `to` through findInput.
		Port* findInput(PortId id) { return findDeclared(m_inputs, id); }
		const Port* findInput(PortId id) const { return findDeclared(m_inputs, id); }
		Port* findOutput(PortId id) { return findDeclared(m_outputs, id); }
		const Port* findOutput(PortId id) const { return findDeclared(m_outputs, id); }

		// Whether a port in `direction` already carries `name`. Backs the port-name-uniqueness
		// invariant that name-addressed serialization relies on: addInput/addOutput assert on a
		// duplicate (an author bug), addDynamicPort rejects one, and the editing layer guards a
		// boundary-pin rename with it. Uniqueness is per-direction (an input and an output may share
		// a name — an edge's from/to imply which).
		bool hasPortNamed(Port::Direction direction, const std::string& name) const
		{
			const std::vector<Port>& ports = (direction == Port::Direction::Input) ? m_inputs : m_outputs;
			for (const Port& port : ports)
			{
				if (port.name() == name)
					return true;
			}
			return false;
		}

		// Configuration values — distinct from ports (see Param). Non-connectable; the scheduler
		// never touches them. compute() reads them via param(m_radius).get<T>().
		//
		// READ-ONLY to everyone but the node itself. A param is RECIPE, so changing one is a
		// document edit that must invalidate the node — and the only way to guarantee that is to
		// make the write and the invalidation one operation (setParam, below). Handing out a
		// mutable Param& made "mutate, then remember to markDirty" the caller's job, in three
		// different call sites; forgetting it left the graph serving a stale result.
		//
		// Addressed the same two ways ports are: by POSITION to iterate them in declaration order,
		// by IDENTITY for a node's own stored handles.
		std::size_t paramCount() const { return m_params.size(); }
		const Param& param(std::size_t i) const { return m_params[i]; }
		const Param& param(PortId id) const { return *checked(findParam(id)); }

		// Resolve a param by its stable PortId, or nullptr — the param twin of findInput /
		// findOutput, for an id that came from elsewhere and may be stale.
		const Param* findParam(PortId id) const { return findDeclared(m_params, id); }

		// THE seam for changing a param: type-check, commit and invalidate as one operation.
		// Returns false — changing nothing at all — when `id` names no param of this node or when
		// `value`'s payload type is not the param's DECLARED type ("the type is the schema"; an
		// empty value is likewise refused, since a param always holds one). The bool result follows
		// removeNode / disconnect / removePort: a primitive reports, it does not decide.
		//
		// Callers that hold a typed value use the template below; callers holding an already
		// type-erased one — a decoded document value, a value an editor widget just wrote — pass
		// the PortValue straight through.
		bool setParam(PortId id, PortValue value);

		// Typed convenience: builds the PortValue and commits through the same seam, so a caller
		// with a compile-time T does not spell out the erasure. The non-template overload above
		// still wins for an actual PortValue argument.
		template <typename T>
		bool setParam(PortId id, T value);

		// This node's RECIPE VERSION: bumped exactly when its definition changes (a param committed
		// through setParam, a structural edit by one of Graph's primitives). Starts at 1, so it never
		// equals a freshly prepared Evaluation's `computedAt` of 0.
		//
		// This is how invalidation reaches evaluations WITHOUT the definition knowing about them.
		// A definition holds no list of its evaluations — deliberately, since they are host-owned —
		// so an edit cannot walk them to drop values. It bumps this, and each evaluation notices the
		// mismatch when it next runs: invalidation is PULLED, never pushed, and an edit costs O(1)
		// however many streams are running. (This replaced a single `m_dirty` bool, which could only
		// describe one run's staleness and so could not serve two evaluations at once.)
		std::uint64_t version() const { return m_version; }

		// The graph this node CONTAINS, or nullptr for an ordinary node. The scheduler asks this of
		// every node while building its execution plan and expands the whole nesting tree into one
		// flat plan (ADR-0009) — so it asks the structural question it actually has ("do you contain
		// a graph?") rather than dynamic_cast-ing for a class identity it doesn't otherwise care
		// about, and a future graph-containing node needs no scheduler change.
		//
		// CONST, deliberately: a contained graph is a DEFINITION, and a definition read through here
		// may be shared between several nodes (a template backing N linked groups — ADR-0013). Mutable
		// access to an interior belongs to the one kind that owns one outright, InlineGroupNode::inner,
		// which is what makes "a linked group is read-only in place" a property of the type rather
		// than a rule every caller has to remember.
		virtual const Graph* innerGraph() const { return nullptr; }

		// The inner boundary pin that this node's port `outer` mirrors, or the null PortId.
		// Meaningful only alongside innerGraph(): the two together ARE the group seam the scheduler
		// drives — expand the contained graph, and know which inner pin each outer port crosses to.
		// Keeping it here (rather than casting to a concrete group class) is what lets a future
		// graph-containing node work with no scheduler change.
		virtual PortId innerPin(PortId /*outer*/) const { return PortId{}; }

		// (Readiness — "every REQUIRED input carries a value", ADR-0007 — followed the values into
		// the Evaluation: `evaluation.ready(id)` for a host, `nodeEvaluation.ready()` inside compute.
		// It is a join of declaration and runtime, so it belongs to the side that owns the values.)

		// The node's work: read this node's inputs, write this node's outputs, both through
		// `evaluation`. CONST: a node may not write to its own definition, which is what makes it
		// safe for several evaluations to run one definition at once. The scheduler calls this in
		// dependency order, only when the node is ready (see above).
		virtual void compute(NodeEvaluation& evaluation) const = 0;

	protected:
		explicit Node(std::string name)
			: m_name(std::move(name))
		{
		}

		// Declare a port in the subclass constructor; returns its stable PortId, which the node
		// keeps as a named member and passes to input() / output() inside compute(). An ID, not a
		// position: a node that later grows or loses a dynamic pin would see a stored index shift
		// under it, and the id is also what an edge and a boundary handle reference. `presence`
		// (input only) declares whether a value on this input is Required for the node to be ready
		// (the default) or Optional — an empty Optional input does not suppress the node, for a
		// Select/Merge branch its compute() checks for presence and picks a live one (ADR-0007).
		template <typename T>
		PortId addInput(std::string name, Presence presence = Presence::Required);
		template <typename T>
		PortId addOutput(std::string name);

		// Declare a port MIRRORING an existing port's declared type, with no compile-time T. A
		// port's type is a shared PortType flyweight, so a node that DERIVES its interface from
		// another node's ports — a group mirroring its inner boundary pins — copies that flyweight
		// instead of needing the type. Which means it mirrors ANY type, including one no registry
		// knows about: a group's ports are derived, not user-chosen, so they must not be limited to
		// the registered set the way an addable dynamic pin is.
		PortId addInputLike(std::string name, const PortType& type, Presence presence = Presence::Required);
		PortId addOutputLike(std::string name, const PortType& type);

		// Declare a configuration param, seeded with `defaultValue`; returns its stable PortId, the
		// same handle shape a port declaration returns. Read it in compute() via
		// param(m_radius).get<T>(); the adapter edits it via param(i).set<T>() while iterating.
		template <typename T>
		PortId addParam(std::string name, T defaultValue);

	private:
		friend class Graph; // assigns the id when the node is added, removes ports, and bumps versions

		void setId(NodeId id) { m_id = id; }

		// Record that this node's recipe changed. Private, and reached only through the operations
		// that actually change it — setParam here, Graph's structural primitives there — so there is
		// no public "mutate, then remember to bump" protocol to forget half of.
		void bumpVersion() { ++m_version; }

		// Erase a port by id (raw — no edge check). Graph::removePort enforces the
		// no-dangling-edge invariant *before* calling this, so it is Graph-only. Returns whether
		// a port was found + erased; other ports keep their ids (the vector compacts).
		bool removePort(PortId id)
		{
			for (auto it = m_inputs.begin(); it != m_inputs.end(); ++it)
				if (it->id() == id)
				{
					m_inputs.erase(it);
					return true;
				}
			for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it)
				if (it->id() == id)
				{
					m_outputs.erase(it);
					return true;
				}
			return false;
		}

		// Mint the next stable PortId (addInput / addOutput call this). Defined in node.inl.
		PortId nextPortId();

		// Linear scan for a declared thing — a port or a param — by id (a node has few of each).
		// Static so the const and non-const find* share one body.
		template <typename Declared>
		static auto findDeclared(Declared& list, PortId id) -> decltype(&list[0])
		{
			for (auto& d : list)
				if (d.id() == id)
					return &d;
			return nullptr;
		}

		// Assert a find* hit, for the by-identity accessors: they are reached with a node's own
		// declared ids, so a miss is a programming error rather than a case to handle. Defined in
		// node.inl, where <cassert> is already included (flow core stays log-free).
		template <typename D>
		static D* checked(D* declared);

		NodeId m_id{};			// reserved sentinel until the Graph assigns a real id
		PortId m_nextPortId{1}; // per-node counter for EVERY declaration (ports and params alike),
								// so the two can never collide; 0 is the reserved sentinel
		std::string m_name;
		std::vector<Port> m_inputs;
		std::vector<Port> m_outputs;
		std::vector<Param> m_params;
		// Starts at 1, not 0: a freshly prepared Evaluation records `computedAt = 0`, so a node is
		// stale until something actually computes it.
		std::uint64_t m_version = 1;
	};
} // namespace lain::flow

#include "lain/flow/details/node.inl"
