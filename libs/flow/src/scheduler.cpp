#include "lain/flow/scheduler.h"

#include "lain/flow/graph.h"

#include <lain/task/task.h>

#include <map>
#include <set>
#include <vector>

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

	// The dirty closure in topo order (see scheduler.h): every dirty node plus everything reachable
	// downstream of one. topoOrder() is sources-first, so a node's predecessors are already decided
	// by the time it is visited — a node is selected iff it is dirty or any predecessor was selected.
	std::vector<NodeId> Scheduler::runOrder(Graph& graph)
	{
		std::map<NodeId, std::vector<NodeId>> predecessors;
		for (const Graph::Edge& e : graph.edges())
			predecessors[e.to.node].push_back(e.from.node);

		std::set<NodeId> selected;
		std::vector<NodeId> order;
		for (const NodeId id : graph.topoOrder())
		{
			bool run = graph.node(id).dirty();
			if (!run)
			{
				const auto it = predecessors.find(id);
				if (it != predecessors.end())
				{
					for (const NodeId pred : it->second)
					{
						if (selected.count(pred) != 0)
						{
							run = true;
							break;
						}
					}
				}
			}
			if (run)
			{
				selected.insert(id);
				order.push_back(id);
			}
		}
		return order;
	}

	// Copy each connected upstream output into `id`'s matching input, in place. The
	// value is copied, never moved — the source port keeps it, which is what leaves
	// every stage inspectable after a run. Writing only this node's own inputs (and
	// reading already-finished predecessors' outputs) is what makes the push tasks
	// safe to run concurrently.
	void Scheduler::populateInputs(Graph& graph, NodeId id)
	{
		Node& target = graph.node(id);
		// Reset inputs first, so an input with NO current edge (never connected, disconnected, or its
		// source node removed) is empty — the readiness gate then treats them all alike.
		for (PortIndex i = 0; i < target.inputCount(); ++i)
			target.input(i).clear();
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to.node == id)
				target.findInput(e.to.port)->value() = graph.node(e.from.node).findOutput(e.from.port)->value();
		}
	}

	// The single "execute a node" primitive (see scheduler.h) — used by both run strategies and the
	// pull walk so readiness (conditional eval) is handled everywhere.
	void Scheduler::runNode(Graph& graph, NodeId id)
	{
		Node& node = graph.node(id);
		node.clearDirty(); // before compute — an on-request node re-marks itself to refire next run
		populateInputs(graph, id);

		// Conditional eval (ADR-0007): a node computes iff it is READY — every REQUIRED input carries a
		// value (Node::ready()). If a required input is empty (unconnected, or its upstream produced
		// nothing / was itself suppressed), the node does not compute — its outputs are cleared, and
		// that emptiness suppresses downstream. An empty OPTIONAL input (a Select/Merge branch) does not
		// block; compute() checks presence itself. A Gate suppresses by clear()ing its output — "skip"
		// is just no value.
		if (node.ready())
		{
			node.compute();
		}
		else
		{
			for (PortIndex o = 0; o < node.outputCount(); ++o)
				node.output(o).clear();
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

		if (graph.node(id).dirty())
			runNode(graph, id);
	}

	//=========================================================================
	// SerialScheduler
	//=========================================================================
	void SerialScheduler::run(Graph& graph)
	{
		// Recompute only the dirty closure (incremental); a clean node not downstream of a dirty one
		// keeps its cached value. topoOrder within the closure means each node's inputs are ready.
		for (const NodeId id : runOrder(graph))
			runNode(graph, id);
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
		// Only the dirty closure becomes tasks (incremental); a clean node keeps its cached value.
		const std::vector<NodeId> order = runOrder(graph);
		const std::set<NodeId> selected(order.begin(), order.end());

		lain::task::Flow flow;

		// One task per selected node, keyed by id — ids aren't contiguous, so a map, not a vector.
		std::map<NodeId, lain::task::Task> tasks;
		for (const NodeId id : order)
			tasks.emplace(id, flow.emplace([this, &graph, id]()
										   { runNode(graph, id); }));

		// Precede only among selected nodes: a clean predecessor already holds its value, so a task
		// needn't wait on it (and it has no task to wait on).
		for (const Graph::Edge& e : graph.edges())
		{
			if (selected.count(e.from.node) != 0 && selected.count(e.to.node) != 0)
				tasks.at(e.from.node).precede(tasks.at(e.to.node));
		}

		m_executor.run(flow); // blocks until every task finishes
	}
} // namespace lain::flow
