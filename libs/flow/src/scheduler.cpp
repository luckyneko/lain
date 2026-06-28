#include "scheduler.h"

#include <lain/flow/graph.h>
#include <lain/task/task.h>

#include <vector>

namespace lain::flow::detail
{
	namespace
	{
		// Copy each connected upstream output into this node's matching input. The
		// value is copied, never moved — the source port keeps it, which is what
		// leaves every stage inspectable after a run. Writing only this node's own
		// inputs (and reading already-finished predecessors' outputs) is what makes
		// the push tasks safe to run concurrently.
		void populateInputs(Graph& graph, NodeId id)
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
		void evaluate(Graph& graph, NodeId id, std::vector<bool>& visited)
		{
			if (visited[id])
				return;
			visited[id] = true;

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
	} // namespace

	void runPush(Graph& graph, lain::task::Executor& executor)
	{
		lain::task::Flow flow;

		// One task per node, in id order so tasks[id] is node id's task.
		std::vector<lain::task::Task> tasks;
		tasks.reserve(graph.nodeCount());
		for (NodeId id = 0; id < graph.nodeCount(); ++id)
		{
			tasks.push_back(flow.emplace([&graph, id]()
										 {
				Node& node = graph.node(id);
				node.clearDirty(); // an on-request node re-marks itself in compute()
				populateInputs(graph, id);
				node.compute(); }));
		}

		for (const Graph::Edge& e : graph.edges())
			tasks[e.from].precede(tasks[e.to]);

		executor.run(flow);
	}

	void runPull(Graph& graph, NodeId target)
	{
		std::vector<bool> visited(graph.nodeCount(), false);
		evaluate(graph, target, visited);
	}
} // namespace lain::flow::detail
