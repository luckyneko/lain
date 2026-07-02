#include "scheduler.h"

#include <lain/flow/graph.h>
#include <lain/task/task.h>

#include <map>
#include <set>

namespace lain::flow::detail
{
	// Copy each connected upstream output into this node's matching input. The
	// value is copied, never moved — the source port keeps it, which is what
	// leaves every stage inspectable after a run. Writing only this node's own
	// inputs (and reading already-finished predecessors' outputs) is what makes
	// the push tasks safe to run concurrently.
	static void populateInputs(Graph& graph, NodeId id)
	{
		Node& target = graph.node(id);
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to == id)
				target.input(e.inPort).value() = graph.node(e.from).output(e.outPort).value();
		}
	}

	// Depth-first pull. dirty() is cleared *before* compute() so an on-request
	// node can markDirty() itself inside compute() and stay dirty for next pull.
	static void evaluate(Graph& graph, NodeId id, std::set<NodeId>& visited)
	{
		if (visited.count(id) != 0)
			return;
		visited.insert(id);

		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to == id)
				evaluate(graph, e.from, visited);
		}

		Node& node = graph.node(id);
		if (node.dirty())
		{
			node.clearDirty();
			populateInputs(graph, id);
			node.compute();
		}
	}

	void runPush(Graph& graph, lain::task::Executor& executor)
	{
		lain::task::Flow flow;

		// One task per node, keyed by id — ids aren't contiguous, so a map, not a
		// vector, indexes them. topoOrder() enumerates every node (acyclic).
		std::map<NodeId, lain::task::Task> tasks;
		for (const NodeId id : graph.topoOrder())
		{
			tasks.emplace(id, flow.emplace([&graph, id]()
										   {
				Node& node = graph.node(id);
				node.clearDirty(); // an on-request node re-marks itself in compute()
				populateInputs(graph, id);
				node.compute(); }));
		}

		for (const Graph::Edge& e : graph.edges())
			tasks.at(e.from).precede(tasks.at(e.to));

		executor.run(flow);
	}

	void runPull(Graph& graph, NodeId target)
	{
		std::set<NodeId> visited;
		evaluate(graph, target, visited);
	}
} // namespace lain::flow::detail
