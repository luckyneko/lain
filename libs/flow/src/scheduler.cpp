#include "lain/flow/scheduler.h"

#include "lain/flow/graph.h"

#include <lain/task/task.h>

#include <map>
#include <set>

namespace lain::flow
{
	//=========================================================================
	// Scheduler
	//=========================================================================
	void Scheduler::evaluate(Graph& graph, NodeId target)
	{
		std::set<NodeId> visited;
		evaluateUpstream(graph, target, visited);
	}

	// Copy each connected upstream output into `id`'s matching input, in place. The
	// value is copied, never moved — the source port keeps it, which is what leaves
	// every stage inspectable after a run. Writing only this node's own inputs (and
	// reading already-finished predecessors' outputs) is what makes the push tasks
	// safe to run concurrently.
	void Scheduler::populateInputs(Graph& graph, NodeId id)
	{
		Node& target = graph.node(id);
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to.node == id)
				target.findInput(e.to.port)->value() = graph.node(e.from.node).findOutput(e.from.port)->value();
		}
	}

	// Depth-first pull. dirty() is cleared *before* compute() so an on-request node
	// can markDirty() itself inside compute() and stay dirty for the next pull.
	void Scheduler::evaluateUpstream(Graph& graph, NodeId id, std::set<NodeId>& visited)
	{
		if (visited.count(id) != 0)
			return;
		visited.insert(id);

		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to.node == id)
				evaluateUpstream(graph, e.from.node, visited);
		}

		Node& node = graph.node(id);
		if (node.dirty())
		{
			node.clearDirty();
			populateInputs(graph, id);
			node.compute();
		}
	}

	//=========================================================================
	// SerialScheduler
	//=========================================================================
	void SerialScheduler::run(Graph& graph)
	{
		// topoOrder() is sources-first, so a single pass finds every node's inputs
		// already produced by the time it computes.
		for (const NodeId id : graph.topoOrder())
		{
			Node& node = graph.node(id);
			node.clearDirty(); // an on-request node re-marks itself in compute()
			populateInputs(graph, id);
			node.compute();
		}
	}

	//=========================================================================
	// ParallelScheduler
	//=========================================================================
	ParallelScheduler::ParallelScheduler(lain::task::Executor& executor)
		: m_executor(executor)
	{
	}

	void ParallelScheduler::run(Graph& graph)
	{
		lain::task::Flow flow;

		// One task per node, keyed by id — ids aren't contiguous, so a map, not a
		// vector, indexes them. topoOrder() enumerates every node (acyclic).
		std::map<NodeId, lain::task::Task> tasks;
		for (const NodeId id : graph.topoOrder())
		{
			tasks.emplace(id, flow.emplace([this, &graph, id]()
										   {
				Node& node = graph.node(id);
				node.clearDirty(); // an on-request node re-marks itself in compute()
				populateInputs(graph, id);
				node.compute(); }));
		}

		for (const Graph::Edge& e : graph.edges())
			tasks.at(e.from.node).precede(tasks.at(e.to.node));

		m_executor.run(flow); // blocks until every task finishes
	}
} // namespace lain::flow
