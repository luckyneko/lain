#pragma once

// One graph's RUNTIME STATE, apart from the recipe that produced it (ADR-0012).
//
// A `Graph` is a definition: node kinds, params, edges, port declarations, names. An `Evaluation`
// is everything a run produces or remembers — port values, which node was computed at which
// definition version, outstanding recompute requests, why a node's last compute failed, host
// boundary bindings, and one child Evaluation per group node. The scheduler takes both:
//
//     run(const Graph& definition, Evaluation& evaluation)
//
// WHY THEY ARE SEPARATE. The target workload runs one definition N times at once (N video streams
// through one subgraph; a map node). Anything runtime stored on the Node is then shared across all N
// — a data race between evaluations of the same node. Passing the context explicitly is not the
// tasteful option, it is the only one that survives.
//
// LINEAGE IS THE PAIRING RULE. An Evaluation spans in-place edits of its definition — that is where
// version comparison earns its incrementality — and spans a CLONE of it, which carries the same
// versions (ADR-0025: a run reads a clone of the document, so one Evaluation meets a new Graph object
// every run). It never transfers to a *rebuilt* Graph, even one whose node UUIDs match: a rebuilt
// graph is new node objects, so its versions are ones this Evaluation never recorded (a version is
// drawn from one process-wide sequence — node.h), and pairing the two would silently recompute
// everything while keeping records that belong to another document. What tells a clone from a
// rebuild is the definition's LINEAGE (types.h), which a clone carries and a construction mints:
// `prepare` records it and refuses a definition of another one, at the root and for every child — a
// mispairing is a host bug, so it is reported rather than absorbed. Unlike the old address
// comparison, it cannot be fooled by a Graph rebuilt where the old one stood, and it does not reject
// a clone. A host still replaces its document and Evaluation together on New/Open/undo.
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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lain::flow
{
	class Graph;
	class Node;
	class Evaluation;
	class RunControl;
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

		// Whether the run computing this node has been cancelled (RunControl) — for a LONG compute()
		// to poll and return early (ADR-0025). Always false outside a cancellable run.
		//
		// Answering true is taken as this node GIVING UP: whatever it wrote is not trusted, it is not
		// recorded as computed, and it stays stale for the next run. A node that never asks, or asks
		// only while the answer is false, has finished normally and is KEPT even if the cancel landed
		// while it ran — its result is still right for the recipe it read.
		bool cancelled() const;

		// FAIL without throwing: this compute produces nothing, and `reason` says why — the build has
		// no backend for it, its inputs disagree, its settings describe nothing. What a node calls
		// instead of clearing its outputs and logging, so the reason reaches where a host shows a
		// failure (Evaluation::failure) rather than only a log a gui user never sees (ADR-0025,
		// amended 2026-10-02).
		//
		// When the compute returns, the scheduler clears EVERY output, whatever the body wrote, so
		// downstream is suppressed exactly as by an empty output (ADR-0007), and records `reason`
		// (a fixed text if it is empty) as the node's failure. The run carries on. And the node is
		// recorded as COMPUTED: a failure the node reports is its answer to these inputs, kept until
		// they or its recipe change, where a throw stays stale and is retried. That difference is
		// load-bearing, not taste — each stage of a run plans from the whole stale closure, so a
		// failure left stale would be computed again in every stage of one run.
		//
		// A later call replaces an earlier one. A compute that also gives up on a cancel has GIVEN UP
		// (cancelled() above): it is owed, and its reason is not recorded.
		void fail(std::string reason);

	private:
		friend class Evaluation;
		friend class Scheduler; // reads m_sawCancel and m_failure: how the compute ended
		NodeEvaluation(Evaluation& evaluation, const Node& node, NodeId id, const RunControl* control)
			: m_evaluation(&evaluation)
			, m_node(&node)
			, m_id(id)
			, m_control(control)
		{
		}

		Evaluation* m_evaluation;
		const Node* m_node;
		NodeId m_id;
		const RunControl* m_control; // null outside a run a host can cancel
		// Set when cancelled() answered true. Mutable because asking is a read, and safe because a
		// view belongs to the one task computing its node — nothing else holds it.
		mutable bool m_sawCancel = false;
		std::optional<std::string> m_failure; // what fail() said, for the scheduler to record
	};

	// One graph's runtime state. A value the host owns, so retention is ownership rather than policy
	// — the cli drops it after reading its outputs, the gui keeps updating one because the Inspector
	// reads it, and a bounded pool is simply how many it keeps.
	//
	// A caller can move one but not copy it. The ONE copy is a PublishedEvaluation (below): a copy
	// reads through the definition it was prepared against, so a copy that did not also hold that
	// definition would be one scope away from reading a destroyed graph.
	//
	// It is also a TREE: one child Evaluation per group node, reached by the group's NodeId. (A map
	// node will hold N children per node; that is why a child is located by coordinate rather than by
	// a minted id — see EvalPath in CONTEXT.md.)
	class Evaluation
	{
	public:
		Evaluation() = default;
		// Records `definition`'s lineage, so prepare can check it is still being run against the same
		// version history.
		explicit Evaluation(const Graph& definition);

		Evaluation(Evaluation&&) noexcept;
		Evaluation& operator=(Evaluation&&) noexcept;
		Evaluation& operator=(const Evaluation&) = delete;
		~Evaluation(); // the copy constructor is private: see PublishedEvaluation

		// Create/prune storage to match `definition`, recursively for group children, and record it.
		// THE COORDINATOR CALLS THIS BEFORE DISPATCH: after it, every node and every declared port has
		// its slot, so worker tasks only read and write existing entries. A node whose ports changed
		// keeps the values of the ports it still has.
		//
		// Throws std::logic_error if `definition` is of another LINEAGE than the one this Evaluation
		// was built for — a rebuilt definition, whose records here describe another document. A clone
		// of the same definition is accepted: that is the point of it. A
		// CHILD whose interior is of another lineage (a linked template re-resolved, an interior
		// replaced wholesale) is not an error but a fresh start: that child is rebuilt empty.
		void prepare(const Graph& definition);

		// The graph this Evaluation was LAST PREPARED against, or nullptr if default-constructed.
		//
		// Not necessarily the document: once runs read clones, it is the clone the last run read. It
		// is what ready(), describe() and needsRecompute() consult, so it must outlive any read made
		// through this Evaluation — a host that lets it die has to prepare against another graph of
		// the same lineage before reading again. A PublishedEvaluation holds its copy's definition
		// for exactly this reason.
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
		// differs from what this Evaluation last computed it at. Asked of the definition this
		// Evaluation was last prepared against, and of this one node alone — the CLOSURE (anything
		// upstream, anything inside, and a definition other than the prepared one, such as a host's
		// document against an evaluation computed on a clone) is StaleClosure's (staleness.h).
		bool needsRecompute(NodeId id) const;

		// Demand a recompute of one node on the next run. The ordinary closure carries it downstream,
		// so a caller asks for the node that actually changed, not the subtree.
		void requestRecompute(NodeId id);

		// Demand a recompute of everything this Evaluation has prepared, this level and below — the
		// "refresh it all" gesture that Graph::markAllDirty used to be, now addressed to ONE
		// evaluation instead of to a definition that cannot know which of several to refresh.
		// Constructing a fresh Evaluation is the stronger operation: it forgets all retained state.
		void requestRecomputeAll();

		// --- failure ------------------------------------------------------------
		// Why `id`'s last compute FAILED, or nullptr if it did not: what it THREW (the exception's
		// what(), or a fixed text for one that is not a std::exception), or the reason it gave
		// NodeEvaluation::fail. Recorded by the run, in the evaluation, because a failure is something
		// a run produces at one coordinate: a node of one map element or of one linked-group instance,
		// which the NodeId alone could not say (ADR-0025).
		//
		// A node that threw stays stale meanwhile; one that failed without throwing is computed, its
		// outputs empty. Either way the record is cleared by the node's next compute that gets through
		// without failing — computed or suppressed. One that GIVES UP on a cancel ended with no
		// answer, so it leaves the record.
		const std::string* failure(NodeId id) const;

		// Whether any node at this level or below — in any child evaluation — holds a failure. What a
		// host reads to show a group as failed when something inside it is.
		bool hasFailure() const;

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
		friend class Scheduler;			  // the run lease, computedAt bookkeeping and input population
		friend class PublishedEvaluation; // the one caller of the copy constructor, and the fold
		friend class StaleClosure;		  // reads computedAt and the request flag against ANOTHER definition
		friend class NodeRecord;		  // a copy of one NodeState

		// A copy of this Evaluation and its whole child tree: every port value is a refcount bump
		// (a PortValue shares its payload), every child is copied in turn, and the copy is not being
		// run whatever this one is. Private because a copy reads through the same definition as its
		// source and holds nothing that keeps it alive — PublishedEvaluation is what pairs the two.
		Evaluation(const Evaluation& other);

		// Everything one node accumulates across runs.
		struct NodeState
		{
			std::map<PortId, PortValue> inputs;	 // populated from incoming edges before compute
			std::map<PortId, PortValue> outputs; // what compute produced (or was cleared on suppression)
			// The definition version this node was last computed at. Starts at 0, which no live
			// version ever equals (versions start at 1), so a freshly prepared node is stale.
			std::uint64_t computedAt = 0;
			bool recomputeRequested = false;	// an evaluation-local demand, independent of the recipe
			std::optional<std::string> failure; // why the last compute failed, until one gets through
		};

		NodeState* state(NodeId id);
		const NodeState* state(NodeId id) const;

		// Scheduler-only. Populating a node's inputs from its incoming edges, and keeping the version
		// books, are the scheduler's business — a node body writes only its own outputs, and a host
		// neither writes inputs nor records what ran.
		PortValue& inputSlot(NodeId node, PortId port);

		// The view a COMPUTE is handed: node(id), plus the run's control, so the node can ask whether
		// it has been cancelled. Scheduler-only, since only a run has a control to hand over.
		NodeEvaluation node(NodeId id, const RunControl& control);

		// The two halves of "this node ran", deliberately apart and in this order:
		//   clearRecomputeRequest BEFORE compute, so an on-request source that rearms itself from
		//     inside compute() keeps its new request instead of having it wiped afterwards;
		//   markComputed AFTER it, so a compute() that THROWS leaves the node at its old version —
		//     it produced nothing, so it must stay stale and be retried, not be recorded as done.
		void clearRecomputeRequest(NodeId node);
		void markComputed(NodeId node, std::uint64_t version); // ... and clears any failure: this one got through

		// A compute of `node` failed, saying `message` (failure() above). Scheduler-only: a run records
		// a throw on the way out of the compute, before the exception leaves, and a NodeEvaluation::fail
		// once the compute has returned — after markComputed, which would otherwise clear it.
		void recordFailure(NodeId node, std::string message);

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
		// The version history this Evaluation's computedAt values belong to. "None yet" until the
		// first prepare, which adopts it — so a default-constructed child can join any interior.
		Lineage m_lineage;
		std::map<NodeId, NodeState> m_nodes;
		// One entry per graph-containing node, holding ONE evaluation for a group and N for a map.
		// Indirect, because an Evaluation must not move when the vector grows: the scheduler holds
		// child pointers in plan steps across a whole invocation.
		std::map<NodeId, std::vector<std::unique_ptr<Evaluation>>> m_children;
		// Held for the duration of a scheduler invocation. A plain bool guarded by the fact that only
		// the coordinator thread takes it — a worker task never enters the scheduler (fire-and-join).
		bool m_running = false;
	};

	// A copy of one node's RECORD — its port values and its bookkeeping (what it was computed at,
	// whether a recompute is still owed, why its last compute failed) — as a step left it (ADR-0025,
	// M14 slice 7). A run hands one to its RunObserver as each step finishes, and a host FOLDS it into
	// the copy its panes read, so a node's result shows the moment it lands rather than at the end of
	// its stage.
	//
	// Taken by the thread that ran the step, from the slots that step wrote, before anything
	// downstream is allowed to read them — so it is race-free by the same edges that order the run.
	// Copying it is a refcount bump per port. Opaque: only the scheduler makes one and only a
	// PublishedEvaluation reads one, because what it holds is exactly the state neither a host nor a
	// node body may write.
	class NodeRecord
	{
	public:
		// Why the compute that left this record failed, or nullptr — Evaluation::failure, as it stood.
		const std::string* failure() const { return m_state.failure.has_value() ? &*m_state.failure : nullptr; }

	private:
		friend class Scheduler;			  // makes one
		friend class PublishedEvaluation; // folds one

		NodeRecord(Lineage lineage, const Evaluation::NodeState& state)
			: m_lineage(lineage)
			, m_state(state)
		{
		}

		// The version history of the evaluation it was taken from. A record folds only into a copy of
		// that same history: its computedAt means nothing against another.
		Lineage m_lineage;
		Evaluation::NodeState m_state;
	};

	// A read-only copy of an Evaluation, together with the definition it reads through (ADR-0025: a
	// host's panes read one while a run holds the working evaluation). The copy's ready(),
	// describe() and needsRecompute() consult the graph its source was last prepared against — in a
	// gui host, the clone the run read — and every child's reads lead into that graph's interiors. So
	// the definition travels WITH the copy: holding the root keeps every inline, map and loop
	// interior alive (their nodes own them) and every linked definition too (a LinkedGroupNode shares
	// it). A lifetime the type states rather than one a host has to remember.
	//
	// Only const access goes out. A published evaluation is something a host READS; it can never be
	// handed to Scheduler::run, so a second working evaluation branched off a first (ADR-0025's
	// deferred "evaluation fork") cannot be made by accident. Its three writes — bind(), fold() and
	// owe(), below — each change what the copy SHOWS, never what any run computes: a pending binding
	// shown at once, and a run's progress shown node by node as it lands.
	//
	// Not called a snapshot — that word means the undo document — and not a record of one execution:
	// a host replaces it as newer results land.
	class PublishedEvaluation
	{
	public:
		// Nothing published yet: every read answers empty, not ready, "(empty)".
		PublishedEvaluation() = default;

		// Copy `evaluation` — a refcount bump per port value, the child tree copied in turn — and
		// keep `definition` alive for as long as the copy lives.
		//
		// Throws std::logic_error unless `definition` is the graph `evaluation` was LAST PREPARED
		// against. That is an address comparison, and rightly so: the question here is lifetime,
		// not history — the copy reads through that one object, so it is the object that must be
		// held. A null `definition` is refused for the same reason.
		//
		// Precondition, not checked: nothing writes `evaluation` while it is copied. A host publishes
		// where its coordinator is quiescent — between stages, or once a run has returned.
		PublishedEvaluation(std::shared_ptr<const Graph> definition, const Evaluation& evaluation);

		// Move-only: copying one would be the whole per-port copy again, for a value a host replaces
		// rather than duplicates.
		PublishedEvaluation(PublishedEvaluation&&) noexcept = default;
		PublishedEvaluation& operator=(PublishedEvaluation&&) noexcept = default;
		PublishedEvaluation(const PublishedEvaluation&) = delete;
		PublishedEvaluation& operator=(const PublishedEvaluation&) = delete;

		const Evaluation& evaluation() const { return m_evaluation; }

		// Show a PENDING BINDING at once: a boundary value the host has bound that no run has
		// consumed yet (ADR-0025). The host queues the same value for the working evaluation, which a
		// run owns while it is in flight; without this the Interface pane would read the old value
		// back every frame until the run caught up, and a scalar drag would snap back.
		//
		// Writes the copy only — its source never sees it. A port the copy has no slot for (a pin
		// added since the run this was copied from) is ignored, exactly as Evaluation::bind ignores
		// it: the queued binding still reaches the next run, which prepares that slot first.
		void bind(PortAddress input, PortValue value) { m_evaluation.bind(input, std::move(value)); }

		// Show a node's result the moment it lands (M14 slice 7): replace the record of `node` in the
		// evaluation at `path` with `record`, which a run reported as a step finished
		// (RunObserver::finished). Its values, what it was computed at, its request and its failure
		// all become the record's — the copy then reads as though it had been taken just after that
		// step.
		//
		// Where the copy has fewer elements of a MAP than `path` names — the run has sized them since
		// the copy was taken — the missing ones are created, prepared against the copy's own
		// definition, and read as never computed until their own records land. Nothing is ever
		// dropped: an element a shrunk collection no longer has stays until a publication replaces
		// the copy.
		//
		// Answers false, and changes nothing, when the copy has no such node at that path (its
		// definition lacks it — a node added since — or nothing has been published at all), or when
		// the evaluation there is of another lineage than the record: its versions describe another
		// history.
		bool fold(const EvalPath& path, NodeId node, const NodeRecord& record);

		// Show that a run OWES `node` at `path` (RunObserver::owed): mark its recompute requested in
		// the copy, as the run has in its working evaluation, until its record lands and clears it.
		// Without this, a node whose upstream has just landed reads as current in the copy while it
		// still shows the value it built from the old input. Finds, grows and refuses exactly as
		// fold() does, minus the lineage check, which only a record can make.
		bool owe(const EvalPath& path, NodeId node);

	private:
		// The evaluation at `path` in the copy, creating a map's missing elements on the way (see
		// fold), or nullptr where the copy's definition has no such level.
		Evaluation* locate(const EvalPath& path);

		// Declared FIRST, so it is destroyed LAST: the copy never outlives the graph it reads through,
		// even for the length of its own destructor.
		std::shared_ptr<const Graph> m_definition;
		Evaluation m_evaluation;
	};
} // namespace lain::flow
