#pragma once

// Internal scheduler for lain::flow — the seam between a Graph and lain::task.
// Taskflow owns the push scheduling, so this is thin: lower the DAG onto a flow
// for the push run, plus a hand-written pull walk for on-demand subgraph eval.
// Library-private (src/), not part of the public include surface.

#include <lain/flow/types.h>

namespace lain::task
{
	class Executor;
}

namespace lain::flow
{
	class Graph;

	namespace detail
	{
		// Push: lower the graph onto a tf::Taskflow (one task per node, edges as
		// precedences) and run it on `executor`. Every node fires once its inputs
		// are ready.
		void runPush(Graph& graph, lain::task::Executor& executor);

		// Pull: evaluate `target`'s upstream subgraph on demand, recomputing only
		// dirty nodes (constant nodes stay clean after their first compute;
		// on-request sources re-mark themselves dirty and so refire each pull).
		void runPull(Graph& graph, NodeId target);
	}
}
