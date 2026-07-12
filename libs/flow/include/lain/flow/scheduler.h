#pragma once

// The execution layer for lain::flow. A Scheduler consumes a Graph and evaluates it,
// so the Graph itself stays a pure data model (nodes + edges + topo) with no notion
// of *how* it runs. Two run strategies share one pull path:
//   - SerialScheduler   — single-threaded topo-order run; needs no execution deps.
//   - ParallelScheduler — lowers the DAG onto an injected (caller-owned) lain::task
//                         executor for work-stealing parallelism.
// The pull path — evaluate(), recompute one node's dirty upstream on demand — is
// identical for both, so it lives on the base; only the full run() varies.

#include "lain/flow/types.h"

#include <set>
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
		// The nodes run() must recompute, in topo order: the DIRTY CLOSURE — every dirty node plus
		// everything downstream of one (a node whose input source recomputes must recompute too).
		// A clean node not downstream of any dirty node is skipped and keeps its cached value — this
		// is incremental re-eval. A fresh graph (all nodes dirty) yields every node; Graph::markAllDirty
		// forces that. Shared by both run strategies so they skip identically.
		std::vector<NodeId> runOrder(Graph& graph);

		// Copy each connected upstream output into `id`'s matching input, in place —
		// the source keeps its value, which is what leaves every stage inspectable.
		// Shared by both run strategies and the pull walk.
		void populateInputs(Graph& graph, NodeId id);

		// Evaluate one node: clear dirty, populate inputs, then either run compute() (READY — every
		// required input has a value) or SUPPRESS it (a required input is empty → clear its outputs,
		// don't compute). The single "execute a node" primitive, so both run strategies and the pull
		// walk handle conditional eval identically (ADR-0007).
		void runNode(Graph& graph, NodeId id);

	private:
		// Depth-first pull helper: recompute `id`'s dirty upstream, then `id` itself.
		void evaluateUpstream(Graph& graph, NodeId id, std::set<NodeId>& visited);
	};

	// Single-threaded: one topo-order pass over the graph. No execution dependency,
	// so it's the natural choice for cli / headless runs and tests.
	class SerialScheduler : public Scheduler
	{
	public:
		void run(Graph& graph) override;
	};

	// Parallel: lowers the DAG onto the injected lain::task executor (one task per
	// node, edges as precedences) and runs it to completion. The caller owns the
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
