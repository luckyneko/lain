#include "lain/flow/scheduler.h"

#include "lain/flow/graph.h" // Graph + the boundary nodes a group step crosses through

#include <lain/task/task.h>

#include <map>
#include <set>
#include <vector>

namespace lain::flow
{
	//=========================================================================
	// Scheduler — planning
	//=========================================================================

	// The dirty closure at ONE level, in topo order (see scheduler.h): every dirty node plus
	// everything reachable downstream of one. topoOrder() is sources-first, so a node's
	// predecessors are already decided by the time it is visited — a node is selected iff it is
	// dirty or any predecessor was selected. Note dirty() is virtual: a group reports itself dirty
	// when anything INSIDE it is, which is what pulls an edit made inside a group into the run.
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

	// Flatten one level into the plan, recursing through anything that contains a graph.
	//
	// A plain node becomes one step that both consumes (populateInputs) and produces. A group
	// becomes THREE parts — entry, its inner graph's steps, exit — so its consuming end is the
	// entry step and its producing end is the exit step. Wiring this level's edges between those
	// ends is what stitches the levels into a single DAG.
	void Scheduler::expand(Graph& graph, const std::vector<NodeId>& order, Plan& plan)
	{
		// Where a node's value is consumed and where it becomes available: the same step for a
		// plain node, entry and exit for a group.
		struct Ends
		{
			std::size_t consumer;
			std::size_t producer;
		};
		std::map<NodeId, Ends> ends;
		const std::set<NodeId> selected(order.begin(), order.end());

		for (const NodeId id : order)
		{
			Node& node = graph.node(id);
			Graph* inner = node.innerGraph();
			if (inner == nullptr)
			{
				const std::size_t step = plan.steps.size();
				plan.steps.push_back(Step{Step::Kind::Node, &graph, id, false});
				ends[id] = Ends{step, step};
				continue;
			}

			// Republish this group's inputs only when they can actually have changed: its own dirty
			// flag is set (a structural edit touched it), or an upstream node is being recomputed.
			// If the group is in the run ONLY because something inside it is dirty, its inputs are
			// unchanged — and republishing would dirty the inner boundary and force the whole inner
			// graph to recompute, throwing away inner incrementality.
			bool publish = node.selfDirty();
			if (!publish)
			{
				for (const Graph::Edge& e : graph.edges())
				{
					if (e.to.node == id && selected.count(e.from.node) != 0)
					{
						publish = true;
						break;
					}
				}
			}

			// New values are coming, so the inner boundary IS dirty — mark it before planning the
			// inner level, or the closure below would leave it out and the published values would
			// sit in a node that never republishes them.
			if (publish)
				inner->boundaryInputNode().markDirty();

			const std::size_t entry = plan.steps.size();
			plan.steps.push_back(Step{Step::Kind::GroupEntry, &graph, id, publish});

			const std::size_t innerBegin = plan.steps.size();
			expand(*inner, runOrder(*inner), plan);
			const std::size_t innerEnd = plan.steps.size();

			const std::size_t exit = plan.steps.size();
			plan.steps.push_back(Step{Step::Kind::GroupExit, &graph, id, false});

			// The entry writes what the inner GroupInput republishes, and the exit reads what the
			// inner GroupOutput was delivered — so those two inner steps, when selected, are the
			// only ones that must be ordered against the boundary steps. Everything else inside is
			// ordered transitively by the inner graph's own edges. (Steps from a DEEPER level carry
			// that level's graph pointer, so this skips them.)
			for (std::size_t i = innerBegin; i < innerEnd; ++i)
			{
				const Step& step = plan.steps[i];
				if (step.graph != inner || step.kind != Step::Kind::Node)
					continue;
				if (step.node == inner->boundaryInputNode().id())
					plan.edges.emplace_back(entry, i);
				else if (step.node == inner->boundaryOutputNode().id())
					plan.edges.emplace_back(i, exit);
			}

			ends[id] = Ends{entry, exit};
		}

		// This level's edges, between the selected nodes' producing and consuming ends. A clean
		// predecessor already holds its value, so nothing waits on it (and it has no step to wait
		// on). `order` is topo, so every edge points forward in `plan.steps`.
		for (const Graph::Edge& e : graph.edges())
		{
			const auto from = ends.find(e.from.node);
			const auto to = ends.find(e.to.node);
			if (from != ends.end() && to != ends.end())
				plan.edges.emplace_back(from->second.producer, to->second.consumer);
		}
	}

	Scheduler::Plan Scheduler::buildRunPlan(Graph& graph)
	{
		Plan plan;
		expand(graph, runOrder(graph), plan);
		return plan;
	}

	Scheduler::Plan Scheduler::buildEvalPlan(Graph& graph, NodeId target)
	{
		std::set<NodeId> cone;
		collectUpstream(graph, target, cone);

		// The dirty nodes of that cone, in topo order. Deliberately NOT the dirty closure: the pull
		// path recomputes only what is itself dirty, leaving a clean node on its cached value.
		std::vector<NodeId> order;
		for (const NodeId id : graph.topoOrder())
		{
			if (cone.count(id) != 0 && graph.node(id).dirty())
				order.push_back(id);
		}

		Plan plan;
		expand(graph, order, plan);
		return plan;
	}

	void Scheduler::collectUpstream(Graph& graph, NodeId id, std::set<NodeId>& cone)
	{
		if (!cone.insert(id).second)
			return;
		for (const Graph::Edge& e : graph.edges())
		{
			if (e.to.node == id)
				collectUpstream(graph, e.from.node, cone);
		}
	}

	//=========================================================================
	// Scheduler — execution
	//=========================================================================
	void Scheduler::evaluate(Graph& graph, NodeId target)
	{
		const Plan plan = buildEvalPlan(graph, target);
		for (const Step& step : plan.steps)
			runStep(step);
	}

	void Scheduler::runStep(const Step& step)
	{
		switch (step.kind)
		{
			case Step::Kind::Node:
				runNode(*step.graph, step.node);
				break;
			case Step::Kind::GroupEntry:
				enterGroup(*step.graph, step.node, step.publish);
				break;
			case Step::Kind::GroupExit:
				exitGroup(*step.graph, step.node);
				break;
		}
	}

	// Copy each connected upstream output into `id`'s matching input, in place. The value is
	// SHARED, not deep-copied (a PortValue holds a shared immutable payload) — the source keeps it,
	// which is what leaves every stage inspectable after a run. Writing only this node's own inputs
	// (and reading already-finished predecessors' outputs) is what makes the tasks safe to run
	// concurrently.
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

	// The single "execute a node" primitive (see scheduler.h) — reached from every plan step that
	// runs a node, so readiness (conditional eval) is handled identically everywhere.
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

	// Crossing INTO a group: take the group's own inputs from the parent graph, then hand them to
	// the inner GroupInputNode, which republishes them onto its output pins when its step runs.
	//
	// Suppression crosses for free (ADR-0009): an unready group publishes EMPTY values, the inner
	// nodes see empty required inputs and suppress themselves through the ordinary readiness gate,
	// and that emptiness reaches the inner GroupOutputNode — so the exit step copies out nothing.
	// No "skip this subtree" machinery is needed in the plan.
	void Scheduler::enterGroup(Graph& graph, NodeId id, bool publish)
	{
		Node& node = graph.node(id);
		node.clearDirty(); // the group's own flag; its inner nodes clear theirs in their own steps
		populateInputs(graph, id);
		if (!publish)
			return;

		Graph* inner = node.innerGraph();
		GroupInputNode& boundary = inner->boundaryInputNode();
		for (PortIndex i = 0; i < node.inputCount(); ++i)
		{
			const Port& outer = node.input(i);
			const PortId pin = node.innerPin(outer.id());
			if (pin != PortId{})
				boundary.setValue(pin, outer.value());
		}
	}

	// Crossing OUT of a group: publish what the inner GroupOutputNode was delivered onto this
	// node's own output ports, so the parent graph reads a group exactly like any other node. An
	// outer port with no inner pin (mid-sync, or a mapping that lost its pin) delivers nothing,
	// which suppresses downstream rather than serving a stale value.
	void Scheduler::exitGroup(Graph& graph, NodeId id)
	{
		Node& node = graph.node(id);
		Graph* inner = node.innerGraph();
		GroupOutputNode& boundary = inner->boundaryOutputNode();
		for (PortIndex i = 0; i < node.outputCount(); ++i)
		{
			Port& outer = node.output(i);
			const PortId pin = node.innerPin(outer.id());
			if (pin != PortId{})
				outer.value() = boundary.value(pin);
			else
				outer.clear();
		}
	}

	//=========================================================================
	// SerialScheduler
	//=========================================================================
	void SerialScheduler::run(Graph& graph)
	{
		// The plan is already in a valid serial order — every dependency points backwards — so the
		// walk needs no further ordering work.
		const Plan plan = buildRunPlan(graph);
		for (const Step& step : plan.steps)
			runStep(step);
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
		// One task per plan step, one precedence per plan edge — across EVERY level of nesting at
		// once, so inner nodes of two sibling groups interleave freely and nothing is nested at
		// runtime (no scheduler ever runs from inside a task).
		const Plan plan = buildRunPlan(graph);

		lain::task::Flow flow;
		std::vector<lain::task::Task> tasks;
		tasks.reserve(plan.steps.size());
		for (const Step& step : plan.steps)
		{
			tasks.push_back(flow.emplace([this, step]() // Step is a small value, copied into the task
										 { runStep(step); }));
		}
		for (const auto& edge : plan.edges)
			tasks[edge.first].precede(tasks[edge.second]);

		m_executor.run(flow); // blocks until every task finishes
	}
} // namespace lain::flow
