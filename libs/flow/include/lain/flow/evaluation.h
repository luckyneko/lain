#pragma once

// One graph's RUNTIME STATE, apart from the recipe that produced it (ADR-0012).
//
// A `Graph` is a definition: node kinds, params, edges, port declarations, names. An `Evaluation`
// is everything a run produces or remembers — port values, which node was computed at which
// definition version, outstanding recompute requests, host boundary bindings, and one child
// Evaluation per group node. The scheduler takes both:
//
//     run(const Graph& definition, Evaluation& evaluation)
//
// WHY THEY ARE SEPARATE. The target workload runs one definition N times at once (N video streams
// through one subgraph; a map node). Anything runtime stored on the Node is then shared across all N
// — a data race between evaluations of the same node. Passing the context explicitly is not the
// tasteful option, it is the only one that survives.
//
// OWNERSHIP IS THE PAIRING RULE. A host owns a definition and its Evaluation as one replaceable
// unit. An Evaluation spans in-place edits of its definition — that is where version comparison earns
// its incrementality — but never transfers to a *rebuilt* Graph, even one whose node UUIDs match,
// because per-node versions are runtime counters that restart at zero. So New/Open/undo replace both
// or neither. `prepare` compares the recorded definition as a cheap guard rail; that is address
// identity, so it catches a mispaired call but not a Graph rebuilt where the old one stood. It is a
// guard rail, not the mechanism.
//
// INVALIDATION IS PULLED, NEVER PUSHED. The definition holds no list of its evaluations — deliberately
// — so an edit cannot walk them to drop values. It bumps a per-node version, and each Evaluation
// notices the mismatch the next time it runs. That keeps an edit O(1) in the number of evaluations,
// which matters when there is one per stream.

#include "lain/flow/portvalue.h"
#include "lain/flow/types.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lain::flow
{
	class Graph;
	class Node;
	class Evaluation;
	struct BoundaryPin;

	// The per-node view a running node is handed: `compute(NodeEvaluation&) const` reads its inputs
	// and writes its outputs through this, and nothing else. It is a non-owning window onto storage
	// the coordinator already created (see Evaluation::prepare), so a worker task never grows a
	// shared container — which is what makes concurrent node tasks safe beyond convention.
	class NodeEvaluation
	{
	public:
		// This node's declaration, for a node body that needs to consult its own ports (a Select
		// routing over its dynamic pins) rather than only read known ids.
		const Node& node() const { return *m_node; }

		// The value on one of this node's ports, addressed by the PortId its declaration returned.
		// An input is read-only — a node reads its inputs and writes only its own outputs, which is
		// the isolation the parallel scheduler relies on. Unknown ids assert (see Node's accessors).
		const PortValue& input(PortId id) const;
		const PortValue& output(PortId id) const;
		PortValue& output(PortId id);

		// Whether this node may compute: every REQUIRED input carries a value (ADR-0007). A join of
		// declaration (Port::required) and runtime (is it empty?), which is why it lives on the side
		// that owns values and knows its definition.
		bool ready() const;

		// Ask for this node to be recomputed on the next run — an ON-REQUEST SOURCE (a camera
		// capture) rearming itself from inside compute(). Evaluation-local: it demands work of THIS
		// evaluation and says nothing about the recipe, so it cannot disturb another stream running
		// the same definition.
		void requestRecompute();

	private:
		friend class Evaluation;
		NodeEvaluation(Evaluation& evaluation, const Node& node, NodeId id)
			: m_evaluation(&evaluation)
			, m_node(&node)
			, m_id(id)
		{
		}

		Evaluation* m_evaluation;
		const Node* m_node;
		NodeId m_id;
	};

	// One graph's runtime state. Move-only: it is a value the host owns, so retention is ownership
	// rather than policy — the cli drops it after reading its outputs, the gui keeps updating one
	// because the Inspector reads it, and a bounded pool is simply how many it keeps.
	//
	// It is also a TREE: one child Evaluation per group node, reached by the group's NodeId. (A map
	// node will hold N children per node; that is why a child is located by coordinate rather than by
	// a minted id — see EvalPath in CONTEXT.md.)
	class Evaluation
	{
	public:
		Evaluation() = default;
		// Records `definition` so prepare can check it is still being run against the same one.
		explicit Evaluation(const Graph& definition);

		Evaluation(Evaluation&&) noexcept;
		Evaluation& operator=(Evaluation&&) noexcept;
		Evaluation(const Evaluation&) = delete;
		Evaluation& operator=(const Evaluation&) = delete;
		~Evaluation();

		// Create/prune storage to match `definition`, recursively for group children, and record it.
		// THE COORDINATOR CALLS THIS BEFORE DISPATCH: after it, every node and every declared port has
		// its slot, so worker tasks only read and write existing entries. A node whose ports changed
		// keeps the values of the ports it still has.
		//
		// Throws std::logic_error if `definition` is not the graph this Evaluation was built for —
		// the guard rail against pairing an evaluation with a rebuilt definition, whose restarted
		// version counters would make changed nodes look clean.
		void prepare(const Graph& definition);

		// The definition this Evaluation belongs to, or nullptr if default-constructed.
		const Graph* definition() const { return m_definition; }

		// --- per-node access ---------------------------------------------------
		// The view for one node. Precondition: prepare() has seen it (asserts otherwise).
		NodeEvaluation node(NodeId id);

		bool contains(NodeId id) const { return m_nodes.count(id) != 0; }

		// Whether `id` may compute — every Required input carries a value (ADR-0007). The host-side
		// spelling of NodeEvaluation::ready(); the canvas dims a node that is not.
		bool ready(NodeId id) const;

		// Port-level value presence, distinct from the node-level readiness above — the two used to
		// share the name `ready()` on Port and Node and mean different things.
		bool hasValue(PortAddress port) const;

		// The value on one port, or an empty one if the port has no slot. Reading is const: a host
		// inspects an evaluation while it holds it, and never writes through this.
		const PortValue& value(PortAddress port) const;

		// That value as human-readable text, rendered by the port's declared type (PortType::describe
		// — the shared meta::toString bridge), or "(empty)". Lives here rather than on Port because
		// the value does: a type still describes itself with no central ladder.
		std::string describe(PortAddress port) const;

		// --- staleness ---------------------------------------------------------
		// Whether `id` must be recomputed: an outstanding request, or the definition's version for it
		// differs from what this Evaluation last computed it at.
		bool needsRecompute(NodeId id) const;

		// Demand a recompute of one node on the next run. The ordinary closure carries it downstream,
		// so a caller asks for the node that actually changed, not the subtree.
		void requestRecompute(NodeId id);

		// Demand a recompute of everything this Evaluation has prepared, this level and below — the
		// "refresh it all" gesture that Graph::markAllDirty used to be, now addressed to ONE
		// evaluation instead of to a definition that cannot know which of several to refresh.
		// Constructing a fresh Evaluation is the stronger operation: it forgets all retained state.
		void requestRecomputeAll();

		// --- host boundary binding ---------------------------------------------
		// Supply a graph input's value. The boundary NODES hold none: a bound value is runtime, so it
		// lives here — which is what lets two evaluations of one definition be bound differently.
		// Binding stores the value as that pin's output and requests recompute of the boundary node,
		// which is what carries the new value downstream on the next run.
		//
		// The PortAddress form is the primitive; group entry uses it to publish a group's outer
		// inputs into its child Evaluation, which is the same operation a host performs at the root.
		void bind(PortAddress input, PortValue value);
		void bind(const BoundaryPin& input, PortValue value);

		// A graph output's delivered value — read after a run. (`value(PortAddress)` above; this
		// overload just spells the intent at the host boundary.)
		const PortValue& value(const BoundaryPin& output) const;

		// --- children (N per graph-containing node) -----------------------------
		// A node that contains a graph has one child Evaluation per EVALUATION OF IT: exactly one
		// for a group, and one per element for a map (ADR-0014), which is why a child is addressed
		// by {node, index} rather than by node alone. A group is simply index 0, so every existing
		// caller reads unchanged.
		//
		// Asserts if there is no child there — a group's child exists for as long as the group does,
		// and a map's for as long as its element does.
		Evaluation& child(NodeId group, std::size_t index = 0);
		const Evaluation& child(NodeId group, std::size_t index = 0) const;

		// Whether there is a child at that index. With the default it answers "does this node have a
		// child evaluation at all?", which is what the scheduler asks before expanding into one.
		bool hasChild(NodeId group, std::size_t index = 0) const { return index < childCount(group); }

		// How many evaluations this node has: 0 for an ordinary node, 1 for a group, N for a map.
		std::size_t childCount(NodeId group) const;

		// Set how many evaluations a node has — a MAP's arity, which is known only once the run has
		// produced the collection that determines it (ADR-0014). Growing adds fresh children;
		// shrinking drops the tail, and everything those elements had computed with it.
		//
		// THE COORDINATOR CALLS THIS, between stages. A worker task must never reach it: growing the
		// child vector is exactly the shared-container growth that would make the parallel path
		// unsafe, which is why arity is settled before any of the next stage's tasks start.
		void setChildCount(NodeId node, std::size_t count);

	private:
		friend class NodeEvaluation;
		friend class Scheduler; // the run lease, computedAt bookkeeping and input population

		// Everything one node accumulates across runs.
		struct NodeState
		{
			std::map<PortId, PortValue> inputs;	 // populated from incoming edges before compute
			std::map<PortId, PortValue> outputs; // what compute produced (or was cleared on suppression)
			// The definition version this node was last computed at. Starts at 0, which no live
			// version ever equals (versions start at 1), so a freshly prepared node is stale.
			std::uint64_t computedAt = 0;
			bool recomputeRequested = false; // an evaluation-local demand, independent of the recipe
		};

		NodeState* state(NodeId id);
		const NodeState* state(NodeId id) const;

		// Scheduler-only. Populating a node's inputs from its incoming edges, and keeping the version
		// books, are the scheduler's business — a node body writes only its own outputs, and a host
		// neither writes inputs nor records what ran.
		PortValue& inputSlot(NodeId node, PortId port);

		// The two halves of "this node ran", deliberately apart and in this order:
		//   clearRecomputeRequest BEFORE compute, so an on-request source that rearms itself from
		//     inside compute() keeps its new request instead of having it wiped afterwards;
		//   markComputed AFTER it, so a compute() that THROWS leaves the node at its old version —
		//     it produced nothing, so it must stay stale and be retried, not be recorded as done.
		void clearRecomputeRequest(NodeId node);
		void markComputed(NodeId node, std::uint64_t version);

		// Reconcile one node's port slots against its declarations, keeping the values of ports that
		// survived. Called by prepare, never by a worker.
		static void prepareNode(NodeState& state, const Node& node);

		// A non-blocking, RAII hold on this Evaluation for one scheduler invocation. Scheduling the
		// same Evaluation twice at once is a caller error, so it FAILS rather than waiting: a mutex
		// would hide the mistake and can deadlock on recursive entry, while doing nothing would leave
		// a release build racing. Distinct Evaluations — including two over one definition — are
		// independent and may run concurrently.
		class RunLease
		{
		public:
			// Throws std::logic_error immediately if `evaluation` is already being scheduled.
			explicit RunLease(Evaluation& evaluation);
			~RunLease();
			RunLease(const RunLease&) = delete;
			RunLease& operator=(const RunLease&) = delete;

		private:
			Evaluation* m_evaluation;
		};

		const Graph* m_definition = nullptr;
		std::map<NodeId, NodeState> m_nodes;
		// One entry per graph-containing node, holding ONE evaluation for a group and N for a map.
		// Indirect, because an Evaluation must not move when the vector grows: the scheduler holds
		// child pointers in plan steps across a whole invocation.
		std::map<NodeId, std::vector<std::unique_ptr<Evaluation>>> m_children;
		// Held for the duration of a scheduler invocation. A plain bool guarded by the fact that only
		// the coordinator thread takes it — a worker task never enters the scheduler (fire-and-join).
		bool m_running = false;
	};
} // namespace lain::flow
