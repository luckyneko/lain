#pragma once

// The execution layer for lain::flow. A Scheduler consumes a Graph and evaluates it,
// so the Graph itself stays a pure data model (nodes + edges + topo) with no notion
// of *how* it runs. Two run strategies share one pull path:
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

		// Push: evaluate the whole graph — each node fires once its inputs are ready.
		virtual void run(Graph& graph) = 0;

		// Pull: evaluate just `target`'s upstream subgraph on demand, recomputing only
		// dirty nodes (a constant stays clean after its first compute; an on-request
		// source re-marks itself and so refires each pull). Serial for either strategy.
		void evaluate(Graph& graph, NodeId target);

	protected:
		// One unit of work. A Node step runs a node; the two Group steps cross a group's
		// boundary — see enterGroup / exitGroup.
		struct Step
		{
			enum class Kind
			{
				Node,		// run graph->node(node)
				GroupEntry, // copy the group's outer inputs into its inner GroupInputNode
				GroupExit,	// copy its inner GroupOutputNode's values out to its outer outputs
			};

			Kind kind = Kind::Node;
			Graph* graph = nullptr; // the graph `node` lives in — NOT necessarily the top-level one
			NodeId node;			// the node to run, or the group whose boundary is being crossed
			bool publish = false;	// GroupEntry only: whether the inputs actually need republishing
		};

		// A whole nested graph flattened into one task DAG: steps in a valid serial order, plus
		// the dependency edges a parallel lowering needs (indices into `steps`).
		struct Plan
		{
			std::vector<Step> steps;
			std::vector<std::pair<std::size_t, std::size_t>> edges; // (predecessor, successor)
		};

		// The plan for a full run: the DIRTY CLOSURE at every level — every dirty node plus
		// everything downstream of one, with each group expanded in place.
		Plan buildRunPlan(Graph& graph);

		// The plan for a pull: the dirty nodes in `target`'s upstream cone. Deliberately NOT the
		// dirty closure — a clean node keeps its cached value even if something upstream recomputes,
		// which is the long-standing pull semantic.
		Plan buildEvalPlan(Graph& graph, NodeId target);

		// Execute one step. The single dispatch point, so both strategies handle groups identically.
		void runStep(const Step& step);

		// Evaluate one node: clear dirty, populate inputs, then either run compute() (READY — every
		// required input has a value) or SUPPRESS it (a required input is empty → clear its outputs,
		// don't compute). ADR-0007.
		void runNode(Graph& graph, NodeId id);

		// Copy each connected upstream output into `id`'s matching input, in place —
		// the source keeps its value, which is what leaves every stage inspectable.
		void populateInputs(Graph& graph, NodeId id);

	private:
		// The nodes a run must recompute at ONE level, in topo order: the dirty closure.
		std::vector<NodeId> runOrder(Graph& graph);

		// Emit `order`'s nodes (already topo-ordered and selected) into `plan`, recursing into any
		// node that contains a graph, and wire this level's edges between the resulting steps.
		void expand(Graph& graph, const std::vector<NodeId>& order, Plan& plan);

		// Publish the group's outer input values into its inner GroupInputNode (when `publish`),
		// after populating those outer inputs from the parent graph.
		void enterGroup(Graph& graph, NodeId id, bool publish);

		// Copy the group's inner GroupOutputNode values out to its own output ports.
		void exitGroup(Graph& graph, NodeId id);

		// Collect `id` and everything transitively feeding it into `cone`.
		void collectUpstream(Graph& graph, NodeId id, std::set<NodeId>& cone);
	};

	// Single-threaded: walks the execution plan in order. No execution dependency, so it's
	// the natural choice for cli / headless runs and tests.
	class SerialScheduler : public Scheduler
	{
	public:
		void run(Graph& graph) override;
	};

	// Parallel: lowers the execution plan onto the injected lain::task executor (one task
	// per step, one precedence per plan edge) and runs it to completion. The caller owns the
	// executor, so worker count and lifetime stay explicit.
	class ParallelScheduler : public Scheduler
	{
	public:
		explicit ParallelScheduler(lain::task::Executor& executor);
		void run(Graph& graph) override;

	private:
		lain::task::Executor& m_executor;
	};
} // namespace lain::flow
