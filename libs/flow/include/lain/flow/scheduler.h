#pragma once

// The execution layer for lain::flow. A Scheduler consumes a DEFINITION and updates an
// EVALUATION — `run(const Graph& definition, Evaluation& evaluation)` — so the Graph stays a pure
// data model with no notion of *how* it runs and no runtime state of its own (ADR-0012).
//
// The definition is `const`, and that means CONCURRENTLY READABLE: several evaluations may run over
// one definition at the same time (N video streams through one subgraph). What keeps that safe is
// that every runtime write lands in the evaluation passed in, and that the coordinator prepares all
// evaluation storage before dispatching any task. Reusing ONE evaluation concurrently is a caller
// error and fails immediately — see the run lease in evaluation.h.
//
// Two run strategies share one pull path:
//   - SerialScheduler   — single-threaded topo-order run; needs no execution deps.
//   - ParallelScheduler — lowers the DAG onto the process task pool for work-stealing
//                         parallelism.
//
// Both go through one EXECUTION PLAN (ADR-0009). A plan is a dependency-ordered list of
// steps covering EVERY level of group nesting at once: a node that contains a graph is not
// run as a node, it is *expanded* into an entry step, its inner graph's own steps, and an
// exit step. So a nested graph runs as one flat DAG — no scheduler is ever invoked from
// inside a task, and inner nodes of two sibling groups interleave freely on the pool.
// run() plans the stale closure; evaluate() plans the part of it in one node's upstream cone.
//
// One invocation may build SEVERAL plans, in STAGES (ADR-0014). A map node's arity comes from a
// collection computed during the run, so it cannot be expanded when the first plan is built: it is
// left as a FRONTIER, along with everything downstream of it, and the loop plans again once the
// stage that computes its input has finished. Each stage is still exactly the flat DAG described
// above — nothing is nested at runtime, and the substrate still needs only emplace/precede/run —
// and the gap BETWEEN stages is where a map's child evaluations are created, on the coordinator
// thread, which is what keeps "a worker task never grows evaluation storage" true (ADR-0012).
// A graph with no map raises no frontier and so runs in exactly one stage, as it always has.
//
// What has been done to each frontier so far is STAGING STATE, held per FRONTIER rather than as a
// list of frontiers seen — because a frontier may be raised more than once. A map raises one
// exactly once; a LOOP raises one per ITERATION (ADR-0021), emitting that iteration's interior
// steps and re-raising itself in the same stage, while the coordinator reads each pass's carried
// values out between stages and binds them in as the next pass's inputs. A loop's mandatory count
// is what bounds the staging loop: a map is deferred at most once per enclosing iteration, and
// iterations are bounded, so "plan again" always terminates.

#include "lain/flow/evaluation.h" // Session holds an Evaluation::RunLease by value
#include "lain/flow/runcontrol.h" // run() takes one, so a caller needs the whole type anyway
#include "lain/flow/types.h"

#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace lain::flow
{
	class Graph;
	class Node;
	class Port;

	// Abstract execution strategy. Both entry points are shared: a strategy differs only in how it
	// executes ONE stage's plan (executePlan), so the staging loop, the run lease and the planning
	// live in one place rather than being re-performed by each backend.
	class Scheduler
	{
	public:
		virtual ~Scheduler() = default;

		// Push: evaluate the whole graph into `evaluation` — each node fires once its inputs are
		// ready. Throws std::logic_error if `evaluation` is already being scheduled (see the run
		// lease); distinct evaluations, including two over this same definition, run concurrently.
		void run(const Graph& definition, Evaluation& evaluation);

		// The same run, steered by a host through `control` (runcontrol.h): another thread may cancel
		// it, and read how far it has got, while it is in flight (ADR-0025).
		//
		// A cancelled run stops starting node COMPUTES and plans no further stage. A compute already
		// running finishes — or gives up, if it asks NodeEvaluation::cancelled() — and the crossings
		// between levels (a group's entry and exit, a map's gather, a loop's fold) still run, since
		// they only copy values and keep each owner consistent with its interior. What finished
		// normally is KEPT; what gave up, and everything the run never reached, stays STALE, so the
		// next run picks it up. That last part holds because every stage's closure is written into
		// the evaluation as recompute requests before the stage runs (Plan::closure).
		void run(const Graph& definition, Evaluation& evaluation, RunControl& control);

		// Pull: bring `target` up to date and nothing else — the part of the stale closure in its
		// upstream cone (a constant stays computed after its first run; an on-request source requests
		// its own recompute and so refires each pull; a node downstream of an edit is recomputed, as it
		// is by run()). The rest of the closure stays stale, including what this pull would otherwise
		// make LOOK current: a node outside the cone fed by one inside it keeps a recompute request.
		// Serial for either strategy.
		void evaluate(const Graph& definition, Evaluation& evaluation, NodeId target);

	protected:
		// The entry ritual EVERY scheduler invocation performs, as one RAII object so a backend
		// cannot do half of it: take the evaluation's non-blocking run lease (which throws
		// std::logic_error at once if that evaluation is already being scheduled), then prepare its
		// storage against the definition. After this, every node and every declared port has its
		// slot, so a worker task only reads and writes entries that already exist.
		class Session
		{
		public:
			Session(const Graph& definition, Evaluation& evaluation)
				: m_lease(evaluation)
			{
				evaluation.prepare(definition);
			}

		private:
			Evaluation::RunLease m_lease; // released when the run unwinds, however it unwinds
		};

		// One unit of work, addressed by DEFINITION plus EVALUATION plus node — the same shared
		// definition may appear in a plan several times with different evaluation pointers (a map
		// runs one subgraph N ways), so neither alone names the work.
		struct Step
		{
			enum class Kind
			{
				Node,		// run definition->node(node) into *evaluation
				GroupEntry, // publish the group's outer inputs into its child evaluation's boundary
				GroupExit,	// copy the child's delivered outputs out onto the group's own output ports
				MapExit,	// GATHER every child's delivered outputs into one collection per port
				LoopExit,	// publish the FOLD: the carries' final values, the last iteration's, the count
			};

			Kind kind = Kind::Node;
			const Graph* definition = nullptr; // the graph `node` lives in — NOT necessarily the top level
			Evaluation* evaluation = nullptr;  // the evaluation THAT graph's values live in
			std::size_t level = 0;			   // where that evaluation is: an index into Plan::levels
			NodeId node;					   // the node to run, or the group whose boundary is being crossed
			bool publish = false;			   // GroupEntry only: whether the inputs actually need republishing

			// LoopExit only: how many iterations ran. A coordinator decision carried INTO the step, as
			// `publish` is — a step must stay self-contained, because a task copies it and can consult
			// no staging state. It is also what separates the two zero-iteration cases at the exit: the
			// fold identity (count == 0, carries deliver their seeds) from a fold that never could run.
			std::size_t iterations = 0;
		};

		// A node this stage could not finish: a MAP whose arity is only known once the run has
		// produced its input (ADR-0014), or a LOOP that has more iterations to run (ADR-0021). It and
		// everything downstream of it are left out of the stage; the staging loop prepares it — sizing
		// a map's children, seeding a loop's next pass — and plans again. A loop is the one node that
		// contributes steps AND raises a frontier in the same stage.
		//
		// Being DEFERRED and RAISING one are different things, and only the second is a frontier. Any
		// node whose interior deferred something is itself deferred — it contributes its interior's
		// steps but publishes nothing, since what it would publish is the previous stage's value — and
		// a group or a map in that position raises nothing of its own: the interior's frontier is what
		// brings the level back. That is why preparing a frontier never has a group to prepare.
		//
		// Addressed exactly like a Step, and for the same reason: one shared definition may be
		// mapped in several evaluations at once, so neither half names the work alone. That address
		// is also the key its staging state is kept under.
		struct Frontier
		{
			const Graph* definition = nullptr;
			Evaluation* evaluation = nullptr;
			NodeId node;

			// The same work in the same evaluation — all three fields, since neither half names it
			// alone. Equality rather than an ordering: Staging looks a record up by this, and nothing
			// needs an order over addresses.
			friend bool operator==(const Frontier& a, const Frontier& b)
			{
				return a.definition == b.definition && a.evaluation == b.evaluation && a.node == b.node;
			}
		};

		// A whole nested graph flattened into one task DAG: steps in a valid serial order, plus
		// the dependency edges a parallel lowering needs (indices into `steps`).
		struct Plan
		{
			std::vector<Step> steps;

			// Every evaluation this stage touches, as the COORDINATE a host can use: the EvalPath from
			// the root. A step (and a closure entry) names its evaluation twice — by address, which is
			// what executes, and by level, an index into this — because an address names nothing in
			// the copy a host's panes read, and that is where a RunObserver's reports have to land.
			// Level 0 is the root. Built as expand() recurses, so it costs one path per level.
			std::vector<EvalPath> levels;
			std::vector<std::pair<std::size_t, std::size_t>> edges; // (predecessor, successor)

			// What this stage deferred. EMPTY means the stage covered the whole run, which is the
			// case for every graph that contains no map — so the staging loop stops after one pass
			// and costs exactly what a single-plan run always did. It is what tells the loop to plan
			// again without having to speculatively re-plan to find out.
			std::vector<Frontier> frontiers;

			// This stage's CLOSURE, as the recompute requests that keep it owed until it has run.
			// Staleness assumes a run finishes its closure — a node is selected when it is stale OR
			// downstream of a selected node, and each step records its node as computed — so a run
			// stopped part-way (a cancel, a throw) would leave a node that was selected only because of
			// its upstream looking clean, holding a value built from the old input. The staging loop
			// therefore requests every entry before the stage executes, and each clears as its own
			// step runs.
			//
			// Exactly three kinds of entry, each named where expand() decides it: a node STEP, a node
			// deferred because something UPSTREAM of it was, and a raised FRONTIER. Deliberately not a
			// group or map expanded without republishing, an owner deferred only because its interior
			// was, or an exit: a request on any of those would make it republish next stage — a
			// change to an uncancelled run — and its interior's own entries already keep it owed.
			struct Owed
			{
				Evaluation* evaluation;
				std::size_t level; // into `levels`: where it is reported as owed (RunObserver::owed)
				NodeId node;
			};
			std::vector<Owed> closure;

			// What a PULL leaves of the stale closure, requested for the same reason the closure is: a
			// node outside the target's cone that something in the closure feeds, and whose own record
			// is clean. It is stale only because of what is upstream of it, and this stage may make
			// that clean — so without a request of its own it would look current while showing a value
			// built from the old input. Empty for a full run, whose cone is everything.
			std::vector<Owed> left;
		};

		// What ONE invocation has done to each frontier it raised. Passed down rather than held on
		// the Scheduler, because one Scheduler may be running two evaluations at once — a member
		// would be shared mutable state on an object whose whole contract is that it has none.
		//
		// A RECORD PER FRONTIER, not a list of frontiers seen, because a frontier may be raised
		// REPEATEDLY: a map raises one once, a loop one per iteration (ADR-0021). An append-only list
		// would then grow an entry per iteration and be re-walked on every lookup, so the entry count
		// stays per frontier ADDRESS however many times that frontier comes back.
		class Staging
		{
		public:
			// What has happened to ONE frontier during this invocation.
			struct State
			{
				// How many times it has been prepared, COUNTING THE PASS BEING STARTED — so 1 is the
				// seeding pass and, for a loop, `preparations - 1` is the number of iterations that have
				// completed. A map's whole use of it is that an entry exists at all: it is deferred at
				// most once per enclosing iteration, which is what bounds the number of stages.
				std::size_t preparations = 0;

				// Set when a LOOP's fold is over, holding how many iterations ran. One optional rather
				// than a flag beside a count, so "finished" and "how many" cannot disagree and
				// "finished, count unknown" cannot be written at all.
				std::optional<std::size_t> iterations;
			};

			// This frontier's state, or nullptr if it has never been raised — which is exactly a map's
			// question ("not prepared yet, so defer it") and half of a loop's.
			const State* find(const Frontier& frontier) const;

			// Begin one preparation of this frontier: create its entry on the first, count this pass,
			// and hand back the state so the preparer can record what it decided.
			State& record(const Frontier& frontier);

			// Drop this frontier's entry, so it is staged from scratch again. A LOOP does this to
			// everything inside its interior when it seeds a new iteration: the same subgraph is about
			// to run again with new values, so a map in there must defer and re-prepare rather than be
			// expanded against the previous iteration's children (Scheduler::forgetInterior).
			void forget(const Frontier& frontier);

		private:
			struct Entry
			{
				Frontier frontier;
				State state;
			};
			std::vector<Entry> m_entries; // one per FRONTIER, never one per raise — see above
		};

		// The plan for a full run: the STALE CLOSURE at every level — every node the evaluation says
		// needs recomputing plus everything downstream of one, with each group expanded in place and
		// each not-yet-prepared map left as a frontier.
		Plan buildRunPlan(const Graph& definition, Evaluation& evaluation, const Staging& staging);

		// The plan for a pull: the part of the stale closure in `target`'s upstream cone — what the
		// target needs, and nothing it does not. Whatever else the closure holds that this pull could
		// make look clean is kept owed (Plan::left).
		Plan buildEvalPlan(const Graph& definition, Evaluation& evaluation, NodeId target, const Staging& staging);

		// Execute ONE stage's plan — the only thing the two strategies do differently. It is handed a
		// complete, already-ordered plan, so a backend never plans, never takes the lease and never
		// decides when the run is over. `control` is the run's, threaded down to every step: the
		// Scheduler holds no per-run state, since one may be running two evaluations at once.
		virtual void executePlan(const Plan& plan, RunControl& control) = 0;

		// Walk a stage's steps in order. The plan is already a valid serial order — every dependency
		// points backwards — so this needs no further ordering work. Used by SerialScheduler, and by
		// the pull path for either strategy.
		void runSteps(const Plan& plan, RunControl& control);

		// Execute one step. The single dispatch point, so both strategies handle groups identically
		// — and cancel identically: a cancelled run skips a node COMPUTE step, whose closure request
		// keeps it owed, and still runs every CROSSING. A crossing only copies values, so running it
		// keeps its owner consistent with whatever its interior currently holds, and the interior's
		// own requests keep the owner stale. Skipping one would need state of its own to say so.
		// `path` is the step's level (Plan::levels), which is what its reports are addressed by.
		void runStep(const Step& step, const EvalPath& path, RunControl& control);

		// Evaluate one node: populate its inputs, then either compute() (READY — every required
		// input has a value) or SUPPRESS it (a required input is empty → clear its outputs, don't
		// compute), and record the definition version it was computed at. ADR-0007. A compute() that
		// throws is not recorded as computed: it asks for its node again, and records WHAT it threw
		// against the node (Evaluation::failure) and in the control's count, before the exception
		// leaves — so the node stays stale however it came to be in the run, and a host can say where
		// the failure was. One that GAVE UP on a cancel (it asked NodeEvaluation::cancelled() and heard
		// yes) is treated the same way minus the record, keeping whatever it wrote but not trusting it.
		//
		// It reports to the control's observer as it goes: started() before anything else, finished()
		// with the node's record once the outcome is settled — on the way out of a throw too — and
		// finished() with no record for a node that gave up.
		void runNode(const Graph& definition, Evaluation& evaluation, const EvalPath& path, NodeId id,
					 RunControl& control);

		// Copy each connected upstream output into `id`'s matching input. The value is SHARED, not
		// deep-copied — the source keeps it, which is what leaves every stage inspectable.
		void populateInputs(const Graph& definition, Evaluation& evaluation, NodeId id);

	private:
		// Which plan each stage builds. The two entry points differ in this and in whether a stage
		// may be executed in parallel — everything else about staging is identical, and writing the
		// loop twice is how two paths that do one job drift apart.
		enum class Mode
		{
			Push, // run(): the stale closure, executed through executePlan
			Pull, // evaluate(): the closure's part in the target's upstream cone, always walked serially
		};

		// The staging loop (ADR-0014): plan, execute, prepare what the stage revealed, plan again.
		// Preparation happens HERE, between stages, on the coordinator thread — never inside a task.
		// A cancelled run stops here too: no stage is planned, and no frontier prepared, once the
		// control says so — which is what stops a loop between iterations.
		void runStages(const Graph& definition, Evaluation& evaluation, Mode mode, NodeId target, RunControl& control);

		// (Which nodes a stage recomputes at each level — the stale closure — and whether a group
		// republishes into its interior are not the scheduler's own: they are StaleClosure's
		// (staleness.h), the same query a host asks to show what is out of date.)

		// Emit `order`'s nodes (already topo-ordered and selected) into `plan`, recursing into any
		// node that contains a graph, and wire this level's edges between the resulting steps. A map
		// whose input may still change in this stage is recorded in `plan.frontiers` instead, along
		// with everything downstream of it.
		// `level` is `evaluation`'s index into plan.levels; an interior expanded from here gets a level
		// of its own, one step deeper.
		void expand(const Graph& definition, Evaluation& evaluation, std::size_t level,
					const std::vector<NodeId>& order, Plan& plan, const Staging& staging);

		// Tell the control's observer, if it has one, that a step has finished leaving `id` as it now
		// stands in `evaluation`. Takes the node's record only when someone is listening, so a run
		// nobody watches pays nothing for it.
		static void reportFinished(RunControl& control, const EvalPath& path, const Evaluation& evaluation, NodeId id);

		// Size and fill a deferred map's children, now that the stage which computes its input has
		// finished: populate its own inputs, read the arity from its split inputs, create/prune one
		// child Evaluation per element, and bind each child's boundary — SPLIT inputs by element,
		// BROADCAST inputs whole. Runs on the coordinator thread, between stages, which is what keeps
		// "a worker task never grows evaluation storage" true (ADR-0012).
		//
		// A map that cannot produce elements — an unready input, ragged split lengths, or no split
		// input at all — is left with NO children and its outputs cleared, which suppresses
		// downstream through ADR-0007's ordinary emptiness rule.
		void prepareMap(const Frontier& frontier);

		// How many elements a map will evaluate, or nullopt when it cannot be determined (see above).
		// Reads the map's already-populated inputs in `evaluation`.
		std::optional<std::size_t> mapArity(const Graph& definition, Evaluation& evaluation, NodeId id);

		// Whether an outer port SPLITS its value across the children: its declared type is a
		// collection whose element type is the inner pin's. Anything else broadcasts.
		static bool splits(const Node& node, const Port& outer);

		// Publish the group's outer input values into its CHILD evaluation's boundary input (when
		// `publish`), after populating those outer inputs from the parent graph.
		void enterGroup(const Graph& definition, Evaluation& evaluation, NodeId id, bool publish);

		// Copy the child evaluation's delivered boundary-output values out onto the group's own
		// output ports in the parent evaluation.
		void exitGroup(const Graph& definition, Evaluation& evaluation, NodeId id);

		// Crossing out of a MAP: gather every child's delivered value for each output port into one
		// collection. A child that delivered nothing is a HOLE, and a hole clears the whole output —
		// a std::vector<T> cannot hold one, and quietly shortening it would break the positional
		// correspondence between the input collection and the output (ADR-0014).
		void exitMap(const Graph& definition, Evaluation& evaluation, NodeId id);

		// Seed or advance a deferred LOOP, between stages and on the coordinator thread (ADR-0021).
		// The first pass populates the loop's own inputs, reads its bound and binds the seeds and
		// `index` into its ONE child; every later pass reads the iteration that just ran — its carried
		// outputs and its `continue` — and either ends the fold (a failure, a break, or the bound) or
		// binds those values back in as the next iteration's inputs.
		//
		// Recording how many iterations ran in `state` is what the next plan reads to decide between
		// emitting another iteration and emitting the exit.
		void prepareLoop(const Frontier& frontier, Staging& staging, Staging::State& state);

		// Drop the staging state of everything INSIDE a loop's interior, because the next iteration
		// re-runs that same subgraph with new values: a map in there must be deferred and re-prepared
		// rather than expanded against the previous iteration's children. Recurses through the
		// DEFINITION, so a frontier nested at any depth below this child is forgotten too.
		void forgetInterior(const Graph& inner, Evaluation& child, Staging& staging);

		// Crossing out of a LOOP: publish the whole fold — each carry's FINAL value, each unpaired
		// inner output's LAST-ITERATION value, and the iteration count. Deliberately not exitGroup:
		// that one clears an output with no inner pin, which is exactly the loop's own report.
		//
		// `iterations == 0` is the fold IDENTITY rather than a failure — every carry delivers its
		// seed, as a map's N == 0 gathers an empty vector. Whether the loop could run at all, and
		// whether the fold broke, are asked HERE rather than remembered, which is exitMap's rule.
		void exitLoop(const Graph& definition, Evaluation& evaluation, NodeId id, std::size_t iterations);

		// Collect `id` and everything transitively feeding it into `cone`.
		static void collectUpstream(const Graph& definition, NodeId id, std::set<NodeId>& cone);
	};

	// Single-threaded: walks each stage's plan in order. No execution dependency, so it's
	// the natural choice for cli / headless runs and tests.
	class SerialScheduler : public Scheduler
	{
	protected:
		void executePlan(const Plan& plan, RunControl& control) override;
	};

	// Parallel: lowers each stage's plan onto the PROCESS task pool (one task per step, one
	// precedence per plan edge) and runs it to completion. There is no executor to pass and no
	// pool to own — lain::app starts one for the process and exposes --threads; a run with none
	// started executes the plan inline on the caller, in dependency order.
	//
	// It BLOCKS the calling thread until the whole plan finishes, participating in the pool's
	// work-stealing meanwhile, so it must never be entered from inside a pool task (the
	// fire-and-join contract). A host that must not block calls it from a thread of its own, and
	// that is not the hard part: a run reads its definition on the workers for its whole length, so
	// the host must also keep editing the document without racing it — a run reads a CLONE — and
	// keep its panes off the evaluation the run is writing — they read a published copy
	// (ADR-0025; flowview's runner.h is the host that does both).
	//
	// It takes no isolated pool, because nothing needs one: catch_discover_tests gives every
	// TEST_CASE its own process, so the process pool is already per-case. Trigger for adding one
	// — a caller that must not share the pool. It is two lines then (a
	// `task::Context& m_context = multi::context();` member), at the cost of this header having
	// to include <multi/context.h>, since you cannot forward-declare into a namespace alias.
	class ParallelScheduler : public Scheduler
	{
	protected:
		void executePlan(const Plan& plan, RunControl& control) override;
	};
} // namespace lain::flow
