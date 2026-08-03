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

#include "lain/flow/evaluation.h" // Session holds an Evaluation::RunLease by value
#include "lain/flow/types.h"

#include <cstddef>
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

	// Abstract execution strategy. run() is the varying full-graph push; evaluate()
	// is the shared, serial pull.
	class Scheduler
	{
	public:
		virtual ~Scheduler() = default;

		// Push: evaluate the whole graph into `evaluation` — each node fires once its inputs are
		// ready. Throws std::logic_error if `evaluation` is already being scheduled (see the run
		// lease); distinct evaluations, including two over this same definition, run concurrently.
		virtual void run(const Graph& definition, Evaluation& evaluation) = 0;

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
			};

			Kind kind = Kind::Node;
			const Graph* definition = nullptr; // the graph `node` lives in — NOT necessarily the top level
			Evaluation* evaluation = nullptr;  // the evaluation THAT graph's values live in
			NodeId node;					   // the node to run, or the group whose boundary is being crossed
			bool publish = false;			   // GroupEntry only: whether the inputs actually need republishing
		};

		// A whole nested graph flattened into one task DAG: steps in a valid serial order, plus
		// the dependency edges a parallel lowering needs (indices into `steps`).
		struct Plan
		{
			std::vector<Step> steps;
			std::vector<std::pair<std::size_t, std::size_t>> edges; // (predecessor, successor)
		};

		// The plan for a full run: the STALE CLOSURE at every level — every node the evaluation says
		// needs recomputing plus everything downstream of one, with each group expanded in place.
		Plan buildRunPlan(const Graph& definition, Evaluation& evaluation);

		// The plan for a pull: the stale nodes in `target`'s upstream cone. Deliberately NOT the
		// closure — a node that does not need recomputing keeps its value even if something upstream
		// does, which is the long-standing pull semantic.
		Plan buildEvalPlan(const Graph& definition, Evaluation& evaluation, NodeId target);

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
		// The nodes a run must recompute at ONE level, in topo order: the stale closure. Takes both,
		// because staleness is a comparison BETWEEN them — the definition's per-node version against
		// what this evaluation recorded.
		std::vector<NodeId> runOrder(const Graph& definition, Evaluation& evaluation);

		// Whether `id` is stale in `evaluation`, INCLUDING anything inside it if it contains a graph
		// — the recursive question a group used to answer with a virtual dirty() over mutable state
		// on its inner definition. It is now asked of the matching child Evaluation.
		static bool stale(const Graph& definition, Evaluation& evaluation, NodeId id);

		// Emit `order`'s nodes (already topo-ordered and selected) into `plan`, recursing into any
		// node that contains a graph, and wire this level's edges between the resulting steps.
		void expand(const Graph& definition, Evaluation& evaluation, const std::vector<NodeId>& order, Plan& plan);

		// Publish the group's outer input values into its CHILD evaluation's boundary input (when
		// `publish`), after populating those outer inputs from the parent graph.
		void enterGroup(const Graph& definition, Evaluation& evaluation, NodeId id, bool publish);

		// Copy the child evaluation's delivered boundary-output values out onto the group's own
		// output ports in the parent evaluation.
		void exitGroup(const Graph& definition, Evaluation& evaluation, NodeId id);

		// Collect `id` and everything transitively feeding it into `cone`.
		static void collectUpstream(const Graph& definition, NodeId id, std::set<NodeId>& cone);
	};

	// Single-threaded: walks the execution plan in order. No execution dependency, so it's
	// the natural choice for cli / headless runs and tests.
	class SerialScheduler : public Scheduler
	{
	public:
		void run(const Graph& definition, Evaluation& evaluation) override;
	};

	// Parallel: lowers the execution plan onto the injected lain::task executor (one task
	// per step, one precedence per plan edge) and runs it to completion. The caller owns the
	// executor, so worker count and lifetime stay explicit.
	class ParallelScheduler : public Scheduler
	{
	public:
		explicit ParallelScheduler(lain::task::Executor& executor);
		void run(const Graph& definition, Evaluation& evaluation) override;

	private:
		lain::task::Executor& m_executor;
	};
} // namespace lain::flow
