#include "lain/flow/scheduler.h"

#include "lain/flow/evaluation.h"
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

	// Is this node stale in this evaluation — including anything INSIDE it?
	//
	// A group used to answer the recursive half with a virtual dirty() that walked mutable flags on
	// its inner definition. Staleness is now a comparison between a definition version and what ONE
	// evaluation recorded, so "is anything inside stale?" is a question about that group's CHILD
	// evaluation, and is asked there. Recursion through nested groups falls out, since a child asks
	// its own children the same way.
	bool Scheduler::stale(const Graph& definition, Evaluation& evaluation, NodeId id)
	{
		if (evaluation.needsRecompute(id))
			return true;

		const Graph* inner = definition.node(id).innerGraph();
		if (inner == nullptr || !evaluation.hasChild(id))
			return false;

		Evaluation& child = evaluation.child(id);
		for (const NodeId innerId : inner->nodeIds())
		{
			if (stale(*inner, child, innerId))
				return true;
		}
		return false;
	}

	// The stale closure at ONE level, in topo order (see scheduler.h): every node that needs
	// recomputing plus everything reachable downstream of one. topoOrder() is sources-first, so a
	// node's predecessors are already decided by the time it is visited — a node is selected iff it
	// is stale or any predecessor was selected.
	std::vector<NodeId> Scheduler::runOrder(const Graph& definition, Evaluation& evaluation)
	{
		std::map<NodeId, std::vector<NodeId>> predecessors;
		for (const Graph::Edge& e : definition.edges())
			predecessors[e.to.node].push_back(e.from.node);

		std::set<NodeId> selected;
		std::vector<NodeId> order;
		for (const NodeId id : definition.topoOrder())
		{
			bool run = stale(definition, evaluation, id);
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
	void Scheduler::expand(const Graph& definition, Evaluation& evaluation, const std::vector<NodeId>& order, Plan& plan)
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
			const Node& node = definition.node(id);
			const Graph* inner = node.innerGraph();
			if (inner == nullptr || !evaluation.hasChild(id))
			{
				const std::size_t step = plan.steps.size();
				plan.steps.push_back(Step{Step::Kind::Node, &definition, &evaluation, id, false});
				ends[id] = Ends{step, step};
				continue;
			}

			Evaluation& child = evaluation.child(id);

			// Republish this group's inputs only when they can actually have changed: the group node
			// itself needs recomputing (a structural edit touched it), or an upstream node is being
			// recomputed. If the group is in the run ONLY because something inside it is stale, its
			// inputs are unchanged — and republishing would request recompute of the inner boundary
			// and force the whole inner graph to recompute, throwing away inner incrementality.
			bool publish = evaluation.needsRecompute(id);
			if (!publish)
			{
				for (const Graph::Edge& e : definition.edges())
				{
					if (e.to.node == id && selected.count(e.from.node) != 0)
					{
						publish = true;
						break;
					}
				}
			}

			// New values are coming, so the inner boundary must recompute — ask the CHILD evaluation
			// before planning the inner level, or the closure below would leave the boundary out and
			// the published values would sit in a node that never carries them onward.
			if (publish)
				child.requestRecompute(inner->boundaryInputNode().id());

			const std::size_t entry = plan.steps.size();
			plan.steps.push_back(Step{Step::Kind::GroupEntry, &definition, &evaluation, id, publish});

			const std::size_t innerBegin = plan.steps.size();
			expand(*inner, child, runOrder(*inner, child), plan);
			const std::size_t innerEnd = plan.steps.size();

			const std::size_t exit = plan.steps.size();
			plan.steps.push_back(Step{Step::Kind::GroupExit, &definition, &evaluation, id, false});

			// The entry writes what the inner GroupInput carries onward, and the exit reads what the
			// inner GroupOutput was delivered — so those two inner steps, when selected, are the
			// only ones that must be ordered against the boundary steps. Everything else inside is
			// ordered transitively by the inner graph's own edges. (Steps from a DEEPER level carry
			// that level's definition pointer, so this skips them.)
			for (std::size_t i = innerBegin; i < innerEnd; ++i)
			{
				const Step& step = plan.steps[i];
				if (step.definition != inner || step.kind != Step::Kind::Node)
					continue;
				if (step.node == inner->boundaryInputNode().id())
					plan.edges.emplace_back(entry, i);
				else if (step.node == inner->boundaryOutputNode().id())
					plan.edges.emplace_back(i, exit);
			}

			ends[id] = Ends{entry, exit};
		}

		// This level's edges, between the selected nodes' producing and consuming ends. A predecessor
		// that was not selected already holds its value, so nothing waits on it (and it has no step to
		// wait on). `order` is topo, so every edge points forward in `plan.steps`.
		for (const Graph::Edge& e : definition.edges())
		{
			const auto from = ends.find(e.from.node);
			const auto to = ends.find(e.to.node);
			if (from != ends.end() && to != ends.end())
				plan.edges.emplace_back(from->second.producer, to->second.consumer);
		}
	}

	Scheduler::Plan Scheduler::buildRunPlan(const Graph& definition, Evaluation& evaluation)
	{
		Plan plan;
		expand(definition, evaluation, runOrder(definition, evaluation), plan);
		return plan;
	}

	Scheduler::Plan Scheduler::buildEvalPlan(const Graph& definition, Evaluation& evaluation, NodeId target)
	{
		std::set<NodeId> cone;
		collectUpstream(definition, target, cone);

		// The stale nodes of that cone, in topo order. Deliberately NOT the closure: the pull path
		// recomputes only what is itself stale, leaving an up-to-date node on its existing value.
		std::vector<NodeId> order;
		for (const NodeId id : definition.topoOrder())
		{
			if (cone.count(id) != 0 && stale(definition, evaluation, id))
				order.push_back(id);
		}

		Plan plan;
		expand(definition, evaluation, order, plan);
		return plan;
	}

	void Scheduler::collectUpstream(const Graph& definition, NodeId id, std::set<NodeId>& cone)
	{
		if (!cone.insert(id).second)
			return;
		for (const Graph::Edge& e : definition.edges())
		{
			if (e.to.node == id)
				collectUpstream(definition, e.from.node, cone);
		}
	}

	//=========================================================================
	// Scheduler — execution
	//=========================================================================
	void Scheduler::run(const Graph& definition, Evaluation& evaluation)
	{
		// One evaluation runs once at a time: the lease throws immediately rather than waiting, so an
		// accidental reuse is a deterministic error instead of a race or a deadlock. It is held for
		// the WHOLE invocation, every stage of it, so staging changes nothing about the contract.
		// Preparation happens with it, so all storage exists before any step touches it.
		const Session session(definition, evaluation);
		runStages(definition, evaluation, Mode::Push, NodeId{});
	}

	void Scheduler::evaluate(const Graph& definition, Evaluation& evaluation, NodeId target)
	{
		const Session session(definition, evaluation);
		runStages(definition, evaluation, Mode::Pull, target);
	}

	// The staging loop (ADR-0014). A map's arity comes from a collection computed during the run, so
	// expansion can leave it as a FRONTIER and the loop plans again once the stage that computes its
	// input has finished. Preparing those children happens HERE — between stages, on this thread —
	// which is what keeps ADR-0012's "a coordinator grows evaluation storage, a worker task never
	// does" literally true rather than merely intended.
	//
	// A graph with no map raises no frontier, so the loop runs exactly one stage and builds exactly
	// one plan: the cost and the behaviour of a single-plan run, unchanged.
	void Scheduler::runStages(const Graph& definition, Evaluation& evaluation, Mode mode, NodeId target)
	{
		while (true)
		{
			const Plan plan = (mode == Mode::Push)
								  ? buildRunPlan(definition, evaluation)
								  : buildEvalPlan(definition, evaluation, target);

			if (plan.steps.empty())
				break;

			// The one thing the strategies differ in — except the pull path, which is serial for
			// either of them by design (see evaluate()).
			if (mode == Mode::Push)
				executePlan(plan);
			else
				runSteps(plan);

			// Nothing was deferred, so this stage was the whole run. Checking the frontiers rather
			// than re-planning to discover there is nothing left is what keeps a mapless run at one
			// plan build.
			if (plan.frontiers.empty())
				break;

			// (Slice 4: prepare each frontier's child evaluations here, now that the stage just
			// executed has produced the collection whose size gives their count.)
		}
	}

	void Scheduler::runSteps(const Plan& plan)
	{
		// The plan is already in a valid serial order — every dependency points backwards — so the
		// walk needs no further ordering work.
		for (const Step& step : plan.steps)
			runStep(step);
	}

	void Scheduler::runStep(const Step& step)
	{
		switch (step.kind)
		{
			case Step::Kind::Node:
				runNode(*step.definition, *step.evaluation, step.node);
				break;
			case Step::Kind::GroupEntry:
				enterGroup(*step.definition, *step.evaluation, step.node, step.publish);
				break;
			case Step::Kind::GroupExit:
				exitGroup(*step.definition, *step.evaluation, step.node);
				break;
		}
	}

	// Copy each connected upstream output into `id`'s matching input. The value is SHARED, not
	// deep-copied (a PortValue holds a shared immutable payload) — the source keeps it, which is what
	// leaves every stage inspectable after a run. Writing only this node's own inputs (and reading
	// already-finished predecessors' outputs) is what makes the tasks safe to run concurrently.
	void Scheduler::populateInputs(const Graph& definition, Evaluation& evaluation, NodeId id)
	{
		const Node& target = definition.node(id);

		// Reset inputs first, so an input with NO current edge (never connected, disconnected, or its
		// source node removed) is empty — the readiness gate then treats them all alike.
		for (std::size_t i = 0; i < target.inputCount(); ++i)
			evaluation.inputSlot(id, target.input(i).id()).clear();
		for (const Graph::Edge& e : definition.edges())
		{
			if (e.to.node == id)
				evaluation.inputSlot(id, e.to.port) = evaluation.value(e.from);
		}
	}

	// The single "execute a node" primitive (see scheduler.h) — reached from every plan step that
	// runs a node, so readiness (conditional eval) is handled identically everywhere.
	void Scheduler::runNode(const Graph& definition, Evaluation& evaluation, NodeId id)
	{
		const Node& node = definition.node(id);
		populateInputs(definition, evaluation, id);

		// Satisfy the outstanding request BEFORE computing, so an on-request source that rearms
		// itself inside compute() (evaluation.requestRecompute()) keeps its NEW request.
		evaluation.clearRecomputeRequest(id);

		NodeEvaluation view = evaluation.node(id);

		// Conditional eval (ADR-0007): a node computes iff it is READY — every REQUIRED input carries a
		// value. If a required input is empty (unconnected, or its upstream produced nothing / was itself
		// suppressed), the node does not compute — its outputs are cleared, and that emptiness suppresses
		// downstream. An empty OPTIONAL input (a Select/Merge branch) does not block; compute() checks
		// presence itself. A Gate suppresses by clearing its output — "skip" is just no value.
		if (view.ready())
		{
			node.compute(view);
		}
		else
		{
			for (std::size_t o = 0; o < node.outputCount(); ++o)
				view.output(node.output(o).id()).clear();
		}

		// Recorded only once the node actually got through — a compute() that threw produced
		// nothing, so it must stay stale and be retried rather than be remembered as done. A
		// SUPPRESSED node does reach here, deliberately: ADR-0007 relies on it going clean and empty
		// together, so a stable-off subtree drops out of future closures instead of being re-examined
		// forever.
		evaluation.markComputed(id, node.version());
	}

	// Crossing INTO a group: take the group's own inputs from the parent graph, then BIND them into
	// the child evaluation's boundary input, which carries them onto its output pins.
	//
	// This is the same operation a host performs at the root (evaluation.bind), which is the point:
	// a group's interface and a graph's interface are one mechanism.
	//
	// Suppression crosses for free (ADR-0009): an unready group binds EMPTY values, the inner nodes
	// see empty required inputs and suppress themselves through the ordinary readiness gate, and that
	// emptiness reaches the inner GroupOutputNode — so the exit step copies out nothing. No "skip this
	// subtree" machinery is needed in the plan.
	void Scheduler::enterGroup(const Graph& definition, Evaluation& evaluation, NodeId id, bool publish)
	{
		const Node& node = definition.node(id);
		populateInputs(definition, evaluation, id);
		// The group node itself is now up to date with its recipe: its work IS crossing the boundary.
		evaluation.clearRecomputeRequest(id);
		evaluation.markComputed(id, node.version());
		if (!publish)
			return;

		const Graph* inner = node.innerGraph();
		Evaluation& child = evaluation.child(id);
		const NodeId boundary = inner->boundaryInputNode().id();
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			const Port& outer = node.input(i);
			const PortId pin = node.innerPin(outer.id());
			if (pin != PortId{})
				child.bind(PortAddress{boundary, pin}, evaluation.value(PortAddress{id, outer.id()}));
		}
	}

	// Crossing OUT of a group: publish what the child evaluation delivered to its inner
	// GroupOutputNode onto this node's own output ports, so the parent graph reads a group exactly
	// like any other node. An outer port with no inner pin (mid-sync, or a mapping that lost its pin)
	// delivers nothing, which suppresses downstream rather than serving a stale value.
	void Scheduler::exitGroup(const Graph& definition, Evaluation& evaluation, NodeId id)
	{
		const Node& node = definition.node(id);
		const Graph* inner = node.innerGraph();
		const Evaluation& child = evaluation.child(id);
		const NodeId boundary = inner->boundaryOutputNode().id();
		NodeEvaluation view = evaluation.node(id);
		for (std::size_t i = 0; i < node.outputCount(); ++i)
		{
			const Port& outer = node.output(i);
			const PortId pin = node.innerPin(outer.id());
			if (pin != PortId{})
				view.output(outer.id()) = child.value(PortAddress{boundary, pin});
			else
				view.output(outer.id()).clear();
		}
	}

	//=========================================================================
	// SerialScheduler
	//=========================================================================
	void SerialScheduler::executePlan(const Plan& plan)
	{
		runSteps(plan);
	}

	//=========================================================================
	// ParallelScheduler
	//=========================================================================
	ParallelScheduler::ParallelScheduler(lain::task::Executor& executor)
		: m_executor(executor)
	{
	}

	// Every node's and every port's storage exists before a single task starts (the Session prepared
	// it, and any later stage's storage is created between stages), so a worker only reads and writes
	// entries that are already there — no task grows a shared container.
	//
	// One task per plan step, one precedence per plan edge — across EVERY level of nesting at once,
	// so inner nodes of two sibling groups interleave freely and nothing is nested at runtime (no
	// scheduler ever runs from inside a task).
	void ParallelScheduler::executePlan(const Plan& plan)
	{
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
