#pragma once

#include "lain/flow/param.h"
#include "lain/flow/port.h"
#include "lain/flow/types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace lain::flow
{
	class Graph;		  // a node may CONTAIN one (see innerGraph) — the group-node seam
	class NodeEvaluation; // the per-node runtime view compute() is handed (evaluation.h)

	// How often a node's CONTAINED graph is evaluated. The third and last part of the structural
	// seam, beside innerGraph() and innerPin(), and asked for the same reason as those two: the
	// scheduler needs the FACT, not the class, so a future interior kind needs no scheduler change.
	//
	// An ENUM rather than a bool per kind (ADR-0021): two booleans could express a state that means
	// nothing, and only an enum lets -Wswitch find the sites the NEXT interior kind has to visit.
	//
	// There is deliberately no `None` for a node with no interior. innerGraph() already answers
	// that, and a second source for the same fact could disagree with the first — the shape M7
	// slice 1 and ADR-0014 each deleted. Every reader pairs the two.
	enum class InteriorEvaluation
	{
		Once,		  // a group: one child evaluation, run once (ADR-0009)
		PerElement,	  // a map: one child per ELEMENT of a collection, sized between stages (ADR-0014)
		PerIteration, // a loop: ONE child, re-run per ITERATION, each pass seeding the next (ADR-0021)
	};

	// The ports an ITERATING interior is driven through (ADR-0021) — the fourth part of the
	// structural seam, and the only one that is not derivable from something already declared.
	// A map needs no equivalent: whether an input splits or broadcasts follows from its own port
	// type, so nothing has to be told. A loop's bound, its report, the two reserved pins and above
	// all its CARRY PAIRING are facts about the node that no type can imply, and pairing by NAME
	// was refused because a rename would then silently stop a loop carrying.
	//
	// It is a struct rather than a handful of virtuals so the scheduler asks one question, and so
	// the next fact an interior kind must state costs Node nothing. It is answered BY VALUE (in an
	// optional) rather than by pointer to a stored one: `carries` points into the answering node,
	// so a stored struct would be one move of that node away from dangling.
	struct IterationPorts
	{
		PortId bound;	  // this node's INPUT: at most how many iterations to run
		PortId report;	  // this node's OUTPUT: how many actually ran
		PortId index;	  // inner GroupInput pin the engine WRITES: which iteration this is
		PortId condition; // inner GroupOutput pin the engine READS: run another?

		// innerIn -> innerOut: the value delivered on innerOut at iteration k is what arrives on
		// innerIn at k+1. Owned by the node; never null when this struct is returned at all.
		const std::map<PortId, PortId>* carries = nullptr;
	};

	// One of a node's named PAYLOAD TYPES (ADR-0022): a type some of its declarations are built
	// FROM, chosen by whoever authored the graph rather than fixed by the node's class. A Constant
	// has one ("value"); a Cast has two ("from" and "to"); a Gate/Merge/Select has one.
	//
	// The name says what the type is FOR on this node, which is why a node may have more than one.
	// It is NOT a param: a param is a value compute() reads, written through setParam — this decides
	// what the node DECLARES, and changing it can invalidate edges, which only a Graph can drop. So
	// the writer is Graph::setPayloadType (the primitive) reached through edit::setPayloadType (the
	// gesture); see node.h's private retypePayload and CONTEXT.md's "payload type".
	struct PayloadType
	{
		std::string name;	  // what this type is for on this node ("value", "from", "to")
		const PortType* type; // the type itself — a shared flyweight, never null
	};

	using PayloadTypes = std::vector<PayloadType>;

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

		// The param holding `input`'s DEFAULT, or nullptr if it has none (most inputs). Public
		// because two outsiders need it: the scheduler seeds an unconnected slot from it, and an
		// inspector edits it — through setParam, like any param — to change the value a disconnected
		// pin carries. See addInput(name, Default{...}) for what a default means and when it applies.
		const Param* defaultOf(PortId input) const
		{
			const auto it = m_defaults.find(input);
			return it == m_defaults.end() ? nullptr : findParam(it->second);
		}

		// Rename a port — THE seam for it, because a rename is display-only for the port and not
		// necessarily for what stands behind it. A port declared with a Default has a PARAM carrying
		// the same name, and a param is addressed BY NAME on disk: renaming only the port leaves the
		// document naming a param the reloaded node does not declare, so the default silently reverts
		// to what the constructor gave it. This moves both.
		//
		// Returns false, changing nothing, for an unknown port, an invalid name, or one already taken
		// on that side — a duplicate would make every name-addressed edge through the pair ambiguous
		// on disk. A primitive reports; it does not decide.
		//
		// A rename is NOT a recipe change (nothing computes differently), so it bumps no version —
		// same as Port::setName, which stays available for a caller that has only a Port.
		bool renamePort(PortId port, std::string name);

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

		// How often this node's contained graph is evaluated (see InteriorEvaluation above) — the
		// third part of the structural seam. Meaningful only alongside innerGraph(); a node with no
		// interior answers Once and is never asked.
		//
		// It is what decides how many child evaluations a node has, and how they relate: PerElement
		// makes them PLURAL (one per element, sized between stages because the collection is only
		// computed during the run), PerIteration makes one child SEQUENTIAL (re-run per iteration,
		// each pass seeding the next).
		virtual InteriorEvaluation interiorEvaluation() const { return InteriorEvaluation::Once; }

		// How the engine drives an interior that iterates, or nullptr for every other node — the
		// detail behind InteriorEvaluation::PerIteration (see IterationPorts above). Asked for the
		// same reason as the three seams above it: the scheduler needs the FACT, never the class, so
		// nothing in the execution layer names a node kind.
		virtual std::optional<IterationPorts> iterationPorts() const { return std::nullopt; }

		// This node's named PAYLOAD TYPES (see PayloadType above), in declaration order — EMPTY for
		// most nodes, which declare their ports from a compile-time T and have nothing to choose.
		//
		// A plain accessor rather than the nullable virtual the other structural seams use: the
		// declarations these types build are this node's, so the tagging has to live here anyway —
		// and a virtual answering a fact Node already holds would be a second source able to
		// disagree with the first, which is the shape M7 slice 1 and ADR-0014 each deleted.
		const PayloadTypes& payloadTypes() const { return m_payloadTypes; }

		// The type currently chosen for the payload type `name`, or nullptr if this node has none by
		// that name. What an adapter reads to show the current selection.
		const PortType* payloadType(const std::string& name) const;

		// The declarations — ports and params alike — built from payload type `name`: exactly what
		// a retype would move. A read, like defaultOf and hasPortNamed, and public for the same
		// reason: both writers of a retype are outside this class, and each must know what a retype
		// would touch BEFORE performing one. There is no answer to that question afterwards,
		// because nothing re-checks an edge once connect() has passed it.
		std::vector<PortId> declarationsOf(const std::string& name) const;

		// Whether this node accepts `type` for its payload type `name` — the filter behind a host's
		// dropdown, and the refusal Graph::setPayloadType enforces so a document cannot install a
		// type the node cannot work with.
		//
		// Accepts anything by default. A node that needs a CAPABILITY overrides and asks for it
		// (a Compare requires PortType::isOrderable), which is what keeps its accepted set derived
		// from what the type can do rather than listed and left to rot.
		virtual bool acceptsPayloadType(const std::string& /*name*/, const PortType& /*type*/) const { return true; }

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

		// Declare an input WITH A DEFAULT: a value the node supplies for itself when nothing is
		// wired to it, and which the graph overrides by wiring something in.
		//
		// WHY THIS EXISTS. A param is per-node configuration, and every element of a map evaluates
		// ONE definition (ADR-0014) — so anything that must differ per element cannot be a param, it
		// has to arrive as a value. Before this, a node wanting both spellings declared a param and
		// an Optional input of the same name and reconciled them by hand, in three places that had
		// to agree.
		//
		// The default IS a param underneath — "editable and serialized" is exactly what a Param is —
		// so it round-trips and the inspector edits it as before. What the caller holds is the PORT
		// id; `defaultOf(port)` finds the param behind it.
		//
		// IT SEEDS ONLY AN UNCONNECTED INPUT, and the port stays REQUIRED. That combination is what
		// keeps a default from swallowing suppression: wire a Gate that is off, and the input is
		// empty rather than defaulted, the node is not ready (ADR-0007), and the gate propagates as
		// it should. Unconnected, the slot always carries the default — so compute() may read it
		// without checking.
		template <typename T>
		PortId addInput(std::string name, Default<T> fallback);

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

		// --- declaring FROM a payload type (ADR-0022) ------------------------------------------
		// Declare a named payload type, seeded with `initial` — the type this node's declarations
		// start out built from, and the one an old document with no `types` section keeps.
		void addPayloadType(std::string name, const PortType& initial);

		// Declare a port / param whose type IS the payload type `payload`, and TAG it so a later
		// retype moves it. Same asserts and same id minting as the plain declarations; the type
		// comes from the payload type rather than from a compile-time T.
		//
		// A tagged declaration is retyped IN PLACE (its PortId, name, order and presence all
		// survive), which is what lets an edge that still typechecks survive a retype too.
		PortId addInputOf(const std::string& payload, std::string name, Presence presence = Presence::Required);
		PortId addOutputOf(const std::string& payload, std::string name);
		PortId addParamOf(const std::string& payload, std::string name);

		// Record that a declaration this node ALREADY made is built from payload type `payload`, so a
		// retype moves it too. For a pin the node did not declare itself: a DYNAMIC branch, added
		// through the port-type registry's creator (which has a compile-time T and no idea this node
		// has payload types at all). A Merge tags each branch in onDynamicPortAdded.
		//
		// Ignores a declaration whose type is not the payload type's — a mistyped pin is not made
		// less mistyped by pretending a retype owns it.
		void tagAs(PortId declaration, const std::string& payload);

		// A defaulted input declared from a payload type — the type-erased twin of
		// addInput<T>(name, Default{...}), seeded from the payload type's own default value. Both
		// halves are tagged, so a retype moves the port and the param behind it together.
		PortId addDefaultedInputOf(const std::string& payload, std::string name);

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

		// The index of the payload type `name` in m_payloadTypes, asserted to exist — a node
		// declares from its own payload types, so naming one it never declared is an author bug.
		std::size_t payloadIndex(const std::string& name) const;

		// Retype every declaration built from the payload type `name`, in place. Graph-only, and
		// that is the whole safety of the mechanism: retyping a port can leave an edge whose ends
		// disagree, nothing re-checks an edge after connect(), and a Node cannot reach its Graph to
		// drop one — so Graph::setPayloadType enforces the no-mistyped-edge invariant BEFORE calling
		// this, exactly as it enforces no-dangling-edge before Node::removePort.
		//
		// Returns false, changing nothing, when this node has no payload type by that name or
		// refuses `type` (acceptsPayloadType). Retyping to the type already chosen is a no-op that
		// succeeds and bumps no version.
		//
		// Every tagged PARAM is reset to `type`'s default value (empty if it has none) and named in
		// `reset`, if given. Carrying a value across is not this level's business: a conversion is a
		// registry lookup, and the gesture that owns the reporting owns that too.
		bool retypePayload(const std::string& name, const PortType& type, std::vector<PortId>* reset = nullptr);

		// input PortId -> the PortId of the param holding its default. Empty for most nodes.
		std::map<PortId, PortId> m_defaults;

		NodeId m_id{};			// reserved sentinel until the Graph assigns a real id
		PortId m_nextPortId{1}; // per-node counter for EVERY declaration (ports and params alike),
								// so the two can never collide; 0 is the reserved sentinel
		std::string m_name;
		std::vector<Port> m_inputs;
		std::vector<Port> m_outputs;
		std::vector<Param> m_params;
		PayloadTypes m_payloadTypes;			   // named types this node's declarations are built from
		std::map<PortId, std::size_t> m_payloadOf; // declaration id -> index into m_payloadTypes
		// Starts at 1, not 0: a freshly prepared Evaluation records `computedAt = 0`, so a node is
		// stale until something actually computes it.
		std::uint64_t m_version = 1;
	};
} // namespace lain::flow

#include "lain/flow/details/node.inl"
