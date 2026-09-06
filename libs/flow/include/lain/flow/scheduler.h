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
//   - ParallelScheduler — lowers the DAG onto an injected (caller-owned) lain::task
//                         executor for work-stealing parallelism.
//
// Both go through one EXECUTION PLAN (ADR-0009). A plan is a dependency-ordered list of
// steps covering EVERY level of group nesting at once: a node that contains a graph is not
// run as a node, it is *expanded* into an entry step, its inner graph's own steps, and an
// exit step. So a nested graph runs as one flat DAG — no scheduler is ever invoked from
// inside a task, and inner nodes of two sibling groups interleave freely on the pool.
// run() plans the dirty closure; evaluate() plans the dirty upstream cone of one node.
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
#include "lain/flow/types.h"

#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace lain::task
{
	class Executor; // named only by ParallelScheduler; full type via <lain/task/task.h>
}

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

		// Pull: evaluate just `target`'s upstream subgraph on demand, recomputing only the nodes
		// that need it (a constant stays computed after its first run; an on-request source requests
		// its own recompute and so refires each pull). Serial for either strategy.
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
			std::vector<std::pair<std::size_t, std::size_t>> edges; // (predecessor, successor)

			// What this stage deferred. EMPTY means the stage covered the whole run, which is the
			// case for every graph that contains no map — so the staging loop stops after one pass
			// and costs exactly what a single-plan run always did. It is what tells the loop to plan
			// again without having to speculatively re-plan to find out.
			std::vector<Frontier> frontiers;
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

		// The plan for a pull: the stale nodes in `target`'s upstream cone. Deliberately NOT the
		// closure — a node that does not need recomputing keeps its value even if something upstream
		// does, which is the long-standing pull semantic.
		Plan buildEvalPlan(const Graph& definition, Evaluation& evaluation, NodeId target, const Staging& staging);

		// Execute ONE stage's plan — the only thing the two strategies do differently. It is handed a
		// complete, already-ordered plan, so a backend never plans, never takes the lease and never
		// decides when the run is over.
		virtual void executePlan(const Plan& plan) = 0;

		// Walk a stage's steps in order. The plan is already a valid serial order — every dependency
		// points backwards — so this needs no further ordering work. Used by SerialScheduler, and by
		// the pull path for either strategy.
		void runSteps(const Plan& plan);

		// Execute one step. The single dispatch point, so both strategies handle groups identically.
		void runStep(const Step& step);

		// Evaluate one node: populate its inputs, then either compute() (READY — every required
		// input has a value) or SUPPRESS it (a required input is empty → clear its outputs, don't
		// compute), and record the definition version it was computed at. ADR-0007.
		void runNode(const Graph& definition, Evaluation& evaluation, NodeId id);

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
			Pull, // evaluate(): the stale upstream cone, always walked serially
		};

		// The staging loop (ADR-0014): plan, execute, prepare what the stage revealed, plan again.
		// Preparation happens HERE, between stages, on the coordinator thread — never inside a task.
		void runStages(const Graph& definition, Evaluation& evaluation, Mode mode, NodeId target);

		// The nodes a run must recompute at ONE level, in topo order: the stale closure. Takes both,
		// because staleness is a comparison BETWEEN them — the definition's per-node version against
		// what this evaluation recorded.
		std::vector<NodeId> runOrder(const Graph& definition, Evaluation& evaluation);

		// Whether `id` is stale in `evaluation`, INCLUDING anything inside it if it contains a graph
		// — the recursive question a group used to answer with a virtual dirty() over mutable state
		// on its inner definition. It is now asked of the matching child Evaluation.
		static bool stale(const Graph& definition, Evaluation& evaluation, NodeId id);

		// Emit `order`'s nodes (already topo-ordered and selected) into `plan`, recursing into any
		// node that contains a graph, and wire this level's edges between the resulting steps. A map
		// whose input may still change in this stage is recorded in `plan.frontiers` instead, along
		// with everything downstream of it.
		void expand(const Graph& definition, Evaluation& evaluation, const std::vector<NodeId>& order, Plan& plan,
					const Staging& staging);

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
		void executePlan(const Plan& plan) override;
	};

	// Parallel: lowers each stage's plan onto the injected lain::task executor (one task
	// per step, one precedence per plan edge) and runs it to completion. The caller owns the
	// executor, so worker count and lifetime stay explicit.
	class ParallelScheduler : public Scheduler
	{
	public:
		explicit ParallelScheduler(lain::task::Executor& executor);

	protected:
		void executePlan(const Plan& plan) override;

	private:
		lain::task::Executor& m_executor;
	};
} // namespace lain::flow
