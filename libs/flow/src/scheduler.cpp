#include "lain/flow/scheduler.h"

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h" // Graph + the boundary nodes a group step crosses through

#include <lain/task/task.h>

#include <cassert>
#include <map>
#include <optional>
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

	//=========================================================================
	// Scheduler::Staging — what this invocation has done to each frontier
	//=========================================================================

	// A linear walk, because the entries are per FRONTIER and not per raise: a document holds a
	// handful of nodes that can raise one, and a node that raises its frontier again comes back to
	// its own entry rather than adding another.
	const Scheduler::Staging::State* Scheduler::Staging::find(const Frontier& frontier) const
	{
		for (const Entry& entry : m_entries)
		{
			if (entry.frontier == frontier)
				return &entry.state;
		}
		return nullptr;
	}

	Scheduler::Staging::State& Scheduler::Staging::record(const Frontier& frontier)
	{
		for (Entry& entry : m_entries)
		{
			if (entry.frontier == frontier)
			{
				++entry.state.preparations;
				return entry.state;
			}
		}
		m_entries.push_back(Entry{frontier, State{1, std::nullopt}});
		return m_entries.back().state;
	}

	void Scheduler::Staging::forget(const Frontier& frontier)
	{
		for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
		{
			if (it->frontier == frontier)
			{
				m_entries.erase(it);
				return;
			}
		}
	}

	// Flatten one level into the plan, recursing through anything that contains a graph.
	//
	// A plain node becomes one step that both consumes (populateInputs) and produces. A group
	// becomes THREE parts — entry, its inner graph's steps, exit — so its consuming end is the
	// entry step and its producing end is the exit step. A MAP becomes its children's steps plus a
	// gathering exit, or is deferred to the next stage. Wiring this level's edges between those ends
	// is what stitches the levels into a single DAG.
	//
	// ONE RULE ACROSS ALL THREE KINDS: a node does not publish while its interior has deferred. It
	// still contributes the interior's steps — that is the work that makes progress — but emits no
	// exit and no PRODUCING end, so everything downstream waits a stage instead of recomputing on a
	// value that is last stage's, or (for a gather) on a cleared one. It keeps its CONSUMING end
	// where it has a step that still reads this level's values, which is the group's entry. The loop
	// arm needs the rule to be CORRECT, since the coordinator reads carried values out between
	// stages; a group and a map merely published early, which is why theirs went unnoticed for two
	// milestones.
	void Scheduler::expand(const Graph& definition, Evaluation& evaluation, const std::vector<NodeId>& order, Plan& plan,
						   const Staging& staging)
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

		// Nodes left out of this stage: a deferred map, and then everything reachable downstream of
		// one. `order` is topo, so a node's predecessors are already decided when it is reached —
		// the same shape runOrder uses to grow the stale closure.
		std::set<NodeId> deferred;

		for (const NodeId id : order)
		{
			const Node& node = definition.node(id);
			const Graph* inner = node.innerGraph();

			// Anything fed by a deferred map waits for the stage that map is expanded in.
			bool downstreamOfDeferred = false;
			for (const Graph::Edge& e : definition.edges())
			{
				if (e.to.node == id && deferred.count(e.from.node) != 0)
				{
					downstreamOfDeferred = true;
					break;
				}
			}
			if (downstreamOfDeferred)
			{
				deferred.insert(id);
				continue;
			}

			// A map is recognised BEFORE the no-child case below: it legitimately has zero children
			// (an empty collection, or one that could not be determined), and running it as an
			// ordinary node would silently do nothing instead of gathering.
			const bool isMap = node.interiorEvaluation() == InteriorEvaluation::PerElement && inner != nullptr;
			const bool isLoop = node.interiorEvaluation() == InteriorEvaluation::PerIteration && inner != nullptr;
			if (inner == nullptr || (!isMap && !evaluation.hasChild(id)))
			{
				const std::size_t step = plan.steps.size();
				plan.steps.push_back(Step{Step::Kind::Node, &definition, &evaluation, id, false});
				ends[id] = Ends{step, step};
				continue;
			}

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

			// A MAP is expanded once its children are known to match its input. That is exactly the
			// condition a group republishes under, read the other way round: if this map's input can
			// still change in this stage, its arity can too, so neither its child count nor their
			// bindings can be trusted yet — defer it, and let the loop prepare it once the stage that
			// computes its input has run.
			//
			// The staging check is what makes the SECOND stage expand it instead of deferring it
			// again; preparation also REQUESTS this map's recompute, so that it — and everything
			// downstream of it — is still selected when the plan is rebuilt. A map is prepared at most
			// once per staging entry — and an enclosing loop drops that entry when it seeds a new
			// iteration, which is what makes a map inside a loop re-prepare per pass.
			if (isMap)
			{
				const Frontier frontier{&definition, &evaluation, id};
				if (publish && staging.find(frontier) == nullptr)
				{
					deferred.insert(id);
					plan.frontiers.push_back(frontier);
					continue;
				}

				// N children, all sharing one definition — the target workload, finally literal.
				const std::size_t count = evaluation.childCount(id);
				const std::size_t begin = plan.steps.size();
				const std::size_t raised = plan.frontiers.size();
				for (std::size_t i = 0; i < count; ++i)
					expand(*inner, evaluation.child(id, i), runOrder(*inner, evaluation.child(id, i)), plan, staging);
				const std::size_t end = plan.steps.size();

				// An element's own interior deferred — a nested map sizing its children, a loop part-way
				// through its fold — so not every element has delivered. Gathering now would read one that
				// has not run, and since a single hole clears the whole output (ADR-0014) it would publish
				// a CLEARED collection downstream, to be replaced a stage later. Contribute the work and
				// take no `ends`, so everything downstream waits — the same rule the loop arm below
				// follows, and no frontier of our own: the interior's is what brings this map back.
				if (plan.frontiers.size() > raised)
				{
					deferred.insert(id);
					continue;
				}

				const std::size_t exit = plan.steps.size();
				plan.steps.push_back(Step{Step::Kind::MapExit, &definition, &evaluation, id, false});

				// Every element's inner GroupOutput must be delivered before the gather reads it.
				for (std::size_t i = begin; i < end; ++i)
				{
					const Step& step = plan.steps[i];
					if (step.definition == inner && step.kind == Step::Kind::Node && step.node == inner->boundaryOutputNode().id())
						plan.edges.emplace_back(i, exit);
				}

				// A map is never consumed within a stage it is expanded in — it would have been
				// deferred if a predecessor were running — so the consuming end is never read. Both
				// ends name the exit, which is the step that genuinely produces its outputs.
				ends[id] = Ends{exit, exit};
				continue;
			}

			// A LOOP is expanded ONE ITERATION AT A TIME (ADR-0021), and what it contributes depends on
			// how far its fold has got — which the coordinator settles between stages, so the plan reads
			// it rather than deciding it.
			//
			// Deliberately NOT conditioned on `publish` the way a map is: iteration k's inputs are
			// iteration k-1's outputs, so there is no inner incrementality to protect. Whenever a loop
			// is selected at all — even only because something inside it was edited — it re-folds from
			// its seeds, and re-seeding is what the first preparation does.
			if (isLoop)
			{
				const Frontier frontier{&definition, &evaluation, id};
				const Staging::State* state = staging.find(frontier);
				if (state == nullptr || !state->iterations.has_value())
				{
					// Not seeded yet, or still iterating. An ITERATING loop contributes this iteration's
					// interior steps AND raises itself again — the one node that both emits and defers.
					// Either way its own outputs are not available in this stage, so it takes no `ends`
					// and everything downstream of it waits, exactly as it does for a map.
					//
					// It raises itself only when this iteration will actually FINISH in this stage. If the
					// interior deferred something of its own — a map sizing its children, a nested loop
					// part-way through its own fold — then the values this pass produces are not there yet,
					// and reading them between stages would see an empty carry and call the iteration
					// failed. The interior's own frontier keeps the staging loop going meanwhile, so this
					// costs a stage rather than a special case (ADR-0021: a map inside a loop costs two
					// stages per iteration).
					bool interiorPending = false;
					if (state != nullptr)
					{
						Evaluation& child = evaluation.child(id); // one child, reused every iteration
						const std::size_t raised = plan.frontiers.size();
						expand(*inner, child, runOrder(*inner, child), plan, staging);
						interiorPending = plan.frontiers.size() > raised;
					}
					deferred.insert(id);
					if (!interiorPending)
						plan.frontiers.push_back(frontier);
					continue;
				}

				// The fold is over: one step publishes it, carrying the iteration count the coordinator
				// settled — a task can consult no staging state.
				const std::size_t exit = plan.steps.size();
				plan.steps.push_back(Step{Step::Kind::LoopExit, &definition, &evaluation, id, false, *state->iterations});
				ends[id] = Ends{exit, exit};
				continue;
			}

			// A group from here down: exactly one child evaluation, which is index 0.
			Evaluation& child = evaluation.child(id);

			// New values are coming, so the inner boundary must recompute — ask the CHILD evaluation
			// before planning the inner level, or the closure below would leave the boundary out and
			// the published values would sit in a node that never carries them onward.
			if (publish)
				child.requestRecompute(inner->boundaryInputNode().id());

			const std::size_t entry = plan.steps.size();
			plan.steps.push_back(Step{Step::Kind::GroupEntry, &definition, &evaluation, id, publish});

			const std::size_t innerBegin = plan.steps.size();
			const std::size_t raised = plan.frontiers.size();
			expand(*inner, child, runOrder(*inner, child), plan, staging);
			const std::size_t innerEnd = plan.steps.size();

			// The entry writes what the inner GroupInput carries onward, so it must precede that
			// step — and it must do so whether or not the group goes on to publish, because the
			// interior runs in this stage either way. Wired BEFORE the deferral below for exactly
			// that reason: an entry left unordered would be free to run after the boundary node it
			// feeds, which a serial walk hides and the parallel backend does not.
			// (Steps from a DEEPER level carry that level's definition pointer, so this skips them.)
			for (std::size_t i = innerBegin; i < innerEnd; ++i)
			{
				const Step& step = plan.steps[i];
				if (step.definition == inner && step.kind == Step::Kind::Node && step.node == inner->boundaryInputNode().id())
					plan.edges.emplace_back(entry, i);
			}

			// The interior deferred something of its own, so what this group would publish is the
			// PREVIOUS stage's value wearing this stage's clothes. The final value is right either way,
			// but every downstream node recomputes on it once per intermediate stage and reads a partial
			// result as a finished one. Emit the entry and the inner steps — that is the work that makes
			// progress — and no exit and no `ends`, so everything downstream waits. A group still raises
			// NO frontier: the interior's own is what keeps the staging loop moving, and Scheduler::stale
			// recurses into the child evaluation, so this group is still selected next stage.
			if (plan.frontiers.size() > raised)
			{
				deferred.insert(id);
				// The entry still CONSUMES this level's values, so this level's edges into the group
				// must still be honoured — without an `ends` entry the entry step would be free to
				// publish before the node feeding it had run, which is a wrong value rather than a
				// wasted one. The mirror image of the map arm above: everything downstream of a
				// deferred node is itself deferred and takes no `ends`, so the producing end here is
				// never read, and both ends name the entry.
				ends[id] = Ends{entry, entry};
				continue;
			}

			const std::size_t exit = plan.steps.size();
			plan.steps.push_back(Step{Step::Kind::GroupExit, &definition, &evaluation, id, false});

			// The exit reads what the inner GroupOutput was delivered, so that step must precede it.
			// Everything else inside is ordered transitively by the inner graph's own edges.
			for (std::size_t i = innerBegin; i < innerEnd; ++i)
			{
				const Step& step = plan.steps[i];
				if (step.definition == inner && step.kind == Step::Kind::Node && step.node == inner->boundaryOutputNode().id())
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

	Scheduler::Plan Scheduler::buildRunPlan(const Graph& definition, Evaluation& evaluation, const Staging& staging)
	{
		Plan plan;
		expand(definition, evaluation, runOrder(definition, evaluation), plan, staging);
		return plan;
	}

	Scheduler::Plan Scheduler::buildEvalPlan(const Graph& definition, Evaluation& evaluation, NodeId target,
											 const Staging& staging)
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
		expand(definition, evaluation, order, plan, staging);
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
		// What this invocation has done to each frontier so far. A map is deferred at most once, so
		// this also bounds the loop: at most one stage per map, plus the final one.
		Staging staging;

		while (true)
		{
			const Plan plan = (mode == Mode::Push)
								  ? buildRunPlan(definition, evaluation, staging)
								  : buildEvalPlan(definition, evaluation, target, staging);

			if (plan.steps.empty() && plan.frontiers.empty())
				break;

			// The one thing the strategies differ in — except the pull path, which is serial for
			// either of them by design (see evaluate()).
			if (mode == Mode::Push)
				executePlan(plan);
			else
				runSteps(plan);

			// Nothing was deferred, so this stage was the whole run. Checking the frontiers rather
			// than re-planning to discover there is nothing left is what keeps a mapless run at one
			// plan build — and what stops a self-rearming source from looping forever.
			if (plan.frontiers.empty())
				break;

			// The stage just executed produced the collections these maps map over, so their arity
			// is knowable now and was not before. Sizing and binding their children happens HERE, on
			// the coordinator thread between stages: it is the second coordinator point ADR-0012 left
			// open, and it is what keeps "a worker task never grows evaluation storage" true.
			for (const Frontier& frontier : plan.frontiers)
			{
				// Counted BEFORE the pass happens, so a preparer knows which one it is: for a loop, 1 is
				// the seeding pass and every later one follows exactly one completed iteration.
				Staging::State& state = staging.record(frontier);
				switch (frontier.definition->node(frontier.node).interiorEvaluation())
				{
					case InteriorEvaluation::PerElement:
						prepareMap(frontier);
						break;

					case InteriorEvaluation::PerIteration:
						prepareLoop(frontier, staging, state);
						break;

					case InteriorEvaluation::Once:
						// A group contributes its interior's steps in place. It may be DEFERRED (when that
						// interior deferred something of its own) but never raises a frontier: the interior's
						// own is what brings the level back, so nothing here ever prepares a group.
						assert(false && "flow::Scheduler: a group never raises a frontier");
						break;
				}
			}
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
			case Step::Kind::MapExit:
				exitMap(*step.definition, *step.evaluation, step.node);
				break;
			case Step::Kind::LoopExit:
				exitLoop(*step.definition, *step.evaluation, step.node, step.iterations);
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
		//
		// ...except an input declared WITH A DEFAULT, which is seeded with it instead. Only here, and
		// only because the slot is about to be overwritten by an edge if there is one: the default
		// fills an UNCONNECTED input, never a connected one that produced nothing. That distinction
		// is the whole safety of the feature — a Gate turned off upstream must suppress this node,
		// not be quietly replaced by its default (ADR-0007).
		for (std::size_t i = 0; i < target.inputCount(); ++i)
		{
			const Port& port = target.input(i);
			PortValue& slot = evaluation.inputSlot(id, port.id());
			if (const Param* fallback = target.defaultOf(port.id()))
				slot = fallback->value();
			else
				slot.clear();
		}
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

	//=========================================================================
	// Scheduler — maps
	//=========================================================================

	// SPLIT or BROADCAST, read off the declaration alone (ADR-0014): an outer port whose type is a
	// collection of the inner pin's type hands element i to child i; anything else hands the same
	// value to every child. Nothing about the mode is stored, so nothing can disagree with the port.
	bool Scheduler::splits(const Node& node, const Port& outer)
	{
		const PortType* collection = outer.portType().element == nullptr ? nullptr : &outer.portType();
		if (collection == nullptr)
			return false;

		// It must be a collection OF THIS PIN's type: a vector<T> outer port against a vector<T>
		// inner pin is a broadcast of a whole collection, not a split of it.
		const Graph* inner = node.innerGraph();
		const PortId pin = node.innerPin(outer.id());
		if (inner == nullptr || pin == PortId{})
			return false;
		const Port* innerPin = inner->boundaryInputNode().findOutput(pin);
		return innerPin != nullptr && collection->element->index == innerPin->type();
	}

	// How many elements this map will evaluate: the common length of its SPLIT inputs.
	//
	// nullopt means it cannot run at all, and each reason is a refusal rather than a guess — ragged
	// lengths would silently drop elements, and a map with nothing to iterate is a wiring mistake
	// that an empty result would hide. An empty collection is NOT this case: zero is an answer.
	std::optional<std::size_t> Scheduler::mapArity(const Graph& definition, Evaluation& evaluation, NodeId id)
	{
		const Node& node = definition.node(id);
		if (!evaluation.ready(id))
			return std::nullopt; // a required input carries no value (ADR-0007), so nothing to map

		std::optional<std::size_t> arity;
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			const Port& outer = node.input(i);
			if (!splits(node, outer))
				continue;
			const PortValue& value = evaluation.value(PortAddress{id, outer.id()});
			const std::size_t length = outer.portType().size(value);
			if (arity.has_value() && *arity != length)
				return std::nullopt; // ragged: two collections that must correspond, do not
			arity = length;
		}
		return arity; // nullopt here means NO split input at all — nothing says how many to run
	}

	// Size and fill a deferred map's children, between stages and on the coordinator thread.
	void Scheduler::prepareMap(const Frontier& frontier)
	{
		const Graph& definition = *frontier.definition;
		Evaluation& evaluation = *frontier.evaluation;
		const NodeId id = frontier.node;
		const Node& node = definition.node(id);
		const Graph* inner = node.innerGraph();

		// Its inputs are only now available: the stage that computed them has just finished.
		populateInputs(definition, evaluation, id);

		// The next stage must SELECT this map, or its exit step never runs and it keeps serving the
		// collection it gathered last time. Requesting it here rather than assuming a request is
		// already standing: on the first run of a fresh evaluation there is one, but on a later run
		// where only the INPUT changed the map went clean in the previous run's exit — and by the
		// next stage its producer is clean too, so nothing else would select it. (exitMap clears
		// this again, which is what stops it recurring.)
		evaluation.requestRecompute(id);

		const std::optional<std::size_t> arity = mapArity(definition, evaluation, id);
		if (!arity.has_value())
		{
			// Nothing to map: keep NO children. The exit step asks the same question again and
			// clears the outputs, so the decision lives in exactly one place — and zero children
			// must NOT be read here as "an empty collection", which is a different answer.
			evaluation.setChildCount(id, 0);
			return;
		}

		evaluation.setChildCount(id, *arity);

		// Bind each child's boundary from the map's own inputs: element i of a split, the whole
		// value of a broadcast. This is the same evaluation.bind() a host performs at the root and a
		// group entry performs into its child — one mechanism, N times.
		const NodeId boundary = inner->boundaryInputNode().id();
		for (std::size_t element = 0; element < *arity; ++element)
		{
			Evaluation& child = evaluation.child(id, element);
			child.prepare(*inner);
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const Port& outer = node.input(i);
				const PortId pin = node.innerPin(outer.id());
				if (pin == PortId{})
					continue;
				const PortValue& whole = evaluation.value(PortAddress{id, outer.id()});
				child.bind(PortAddress{boundary, pin},
						   splits(node, outer) ? outer.portType().at(whole, element) : whole);
			}
		}
	}

	// Crossing OUT of a MAP: one collection per output port, gathered from every child.
	void Scheduler::exitMap(const Graph& definition, Evaluation& evaluation, NodeId id)
	{
		const Node& node = definition.node(id);
		const Graph* inner = node.innerGraph();
		const NodeId boundary = inner->boundaryOutputNode().id();
		const std::size_t count = evaluation.childCount(id);
		NodeEvaluation view = evaluation.node(id);

		// "Could this map run at all?" is asked HERE, not remembered from preparation — one rule in
		// one place. It also keeps zero children unambiguous: an EMPTY COLLECTION gathers to an empty
		// vector (a value), while a map that could not determine an arity produces nothing at all.
		const bool runnable = mapArity(definition, evaluation, id).has_value();

		for (std::size_t o = 0; o < node.outputCount(); ++o)
		{
			const Port& outer = node.output(o);
			const PortId pin = node.innerPin(outer.id());
			if (!runnable || pin == PortId{} || outer.portType().gather == nullptr)
			{
				view.output(outer.id()).clear();
				continue;
			}

			std::vector<PortValue> elements;
			elements.reserve(count);
			for (std::size_t element = 0; element < count; ++element)
				elements.push_back(evaluation.child(id, element).value(PortAddress{boundary, pin}));

			// gather() reports a hole by returning nothing, and that clears the whole output: a
			// std::vector<T> cannot hold a hole, and gathering only the survivors would break the
			// positional correspondence between the input collection and this one (ADR-0014). Which
			// element failed is readable from the child evaluations, so nothing needs reporting here
			// — which is just as well, since flow core is log-free.
			view.output(outer.id()) = outer.portType().gather(elements);
		}

		// The map's own work IS this crossing, so it goes clean here — the two halves in the order
		// runNode uses, for the same reasons.
		evaluation.clearRecomputeRequest(id);
		evaluation.markComputed(id, node.version());
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
	// Scheduler — loops
	//=========================================================================

	// One int in a PortValue — the reserved `index` pin the engine writes, and the loop's own
	// report. int on both sides because that is the type a graph can actually drive.
	static PortValue intValue(std::size_t value)
	{
		PortValue slot;
		slot.set<int>(static_cast<int>(value));
		return slot;
	}

	// What a carried output delivers when NO iteration ran: its SEED, which is the value on the
	// outer input paired with it. The pairing is inner pin to inner pin, so this walks innerOut ->
	// innerIn -> the outer input mirroring that pin — all through Node's own seam, so nothing here
	// names a node class. An output that is not carried has no seed and no last iteration.
	static PortValue seedValue(const Node& node, const Evaluation& evaluation, NodeId id, const IterationPorts& ports,
							   PortId innerOut)
	{
		for (const auto& [innerIn, out] : *ports.carries)
		{
			if (out != innerOut)
				continue;
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const Port& outer = node.input(i);
				if (node.innerPin(outer.id()) == innerIn)
					return evaluation.value(PortAddress{id, outer.id()});
			}
		}
		return PortValue{};
	}

	// Seed or advance a deferred loop, between stages and on the coordinator thread (ADR-0021).
	void Scheduler::prepareLoop(const Frontier& frontier, Staging& staging, Staging::State& state)
	{
		const Graph& definition = *frontier.definition;
		Evaluation& evaluation = *frontier.evaluation;
		const NodeId id = frontier.node;
		const Node& node = definition.node(id);
		const Graph* inner = node.innerGraph();
		const std::optional<IterationPorts> ports = node.iterationPorts();
		if (inner == nullptr || !ports.has_value())
		{
			// A node that says PerIteration but answers no ports is inconsistent with itself. End the
			// fold at zero rather than leaving it un-ended: the exit clears its outputs, and the staging
			// loop must never be able to wait on something that will never finish.
			state.iterations = 0;
			return;
		}

		Evaluation& child = evaluation.child(id); // exactly ONE, reused every iteration
		const NodeId inBoundary = inner->boundaryInputNode().id();
		const NodeId outBoundary = inner->boundaryOutputNode().id();

		// The trip bound, read from the loop's own input each pass rather than remembered: it was
		// populated on the seeding pass and cannot change within an invocation. A value that is not
		// an int, or is negative, means no iterations — which is a legitimate answer (the identity),
		// not an error.
		const auto trips = [&]() -> std::size_t
		{
			const PortValue& value = evaluation.value(PortAddress{id, ports->bound});
			if (!value.holds<int>())
				return 0;
			const int bound = value.get<int>();
			return bound <= 0 ? 0 : static_cast<std::size_t>(bound);
		};

		// THE SEEDING PASS. Everything the fold starts from is established here, on this thread,
		// before any of the next stage's tasks run.
		if (state.preparations == 1)
		{
			populateInputs(definition, evaluation, id);

			// Requested ONCE, and that is enough for the whole fold: nothing clears it until exitLoop
			// does, so the loop — and everything downstream of it — stays selected in every stage this
			// invocation plans.
			evaluation.requestRecompute(id);

			// ADR-0007: a node whose required input carries no value does not run at all. Recorded as
			// a finished fold of zero iterations; the exit asks readiness again and clears the outputs,
			// so the rule lives in one place rather than being remembered here.
			if (!evaluation.ready(id))
			{
				state.iterations = 0;
				return;
			}

			// Publish every mirrored input into the interior, exactly as enterGroup publishes a
			// group's — a carried pin's value is its SEED, an unpaired one's is an invariant that
			// simply stays bound for the whole fold.
			//
			// Accepted cost: bind marks the whole inner GroupInputNode, not one pin, so a subtree fed
			// only by an invariant recomputes every iteration anyway.
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const Port& outer = node.input(i);
				const PortId pin = node.innerPin(outer.id());
				if (pin != PortId{})
					child.bind(PortAddress{inBoundary, pin}, evaluation.value(PortAddress{id, outer.id()}));
			}
			child.bind(PortAddress{inBoundary, ports->index}, intValue(0));

			// A bound of zero is the fold IDENTITY, not a failure: no iteration runs and every carry
			// delivers its seed — the same answer a map's N == 0 gives when it gathers an empty vector.
			if (trips() == 0)
				state.iterations = 0;
			return;
		}

		// EVERY LATER PASS follows exactly one completed iteration.
		const std::size_t completed = state.preparations - 1;

		// Read the iteration's results OUT before binding anything back IN. This is only sound
		// because a PortValue's payload is shared and IMMUTABLE (M5 slice 1): these copies keep
		// iteration k's values alive and unchanged while the binds below rebind the very slots
		// iteration k+1 will read. Without that, reuse would need a second child or a deep copy per
		// carry per iteration.
		const PortValue condition = child.value(PortAddress{outBoundary, ports->condition});
		std::vector<std::pair<PortId, PortValue>> carried;
		carried.reserve(ports->carries->size());
		for (const auto& [innerIn, innerOut] : *ports->carries)
		{
			carried.emplace_back(innerIn, child.value(PortAddress{outBoundary, innerOut}));
		}

		// The iteration FAILED if it produced no condition or lost a carried value — a suppressed
		// body, ADR-0007 reaching the engine. The fold stops, and the exit (which asks the same
		// question itself) clears every output: a partial fold that looks finished is worse than no
		// answer at all.
		bool failed = !condition.holds<bool>(); // empty, or somehow not a bool
		for (const auto& carry : carried)
		{
			if (carry.second.empty())
				failed = true;
		}

		// Stop on failure, on the body saying so (`continue` false — an unwired one defaults to
		// true, so a count loop is transparent here), or on the bound. `iterations` is how many
		// actually ran, which is what makes "converged at 7" distinguishable from "hit 100".
		if (failed || !condition.get<bool>() || completed >= trips())
		{
			state.iterations = completed;
			return;
		}

		// Another iteration: each carried value becomes the input it is paired with, and the body is
		// told which pass this is.
		for (const auto& carry : carried)
		{
			child.bind(PortAddress{inBoundary, carry.first}, carry.second);
		}
		child.bind(PortAddress{inBoundary, ports->index}, intValue(completed));

		// The same subgraph is about to run again with new values, so whatever is staged INSIDE it
		// belongs to the iteration that just ended — a map in there must defer and re-prepare rather
		// than be expanded against the previous iteration's children.
		forgetInterior(*inner, child, staging);
	}

	void Scheduler::forgetInterior(const Graph& inner, Evaluation& child, Staging& staging)
	{
		for (const NodeId id : inner.nodeIds())
		{
			const Graph* nested = inner.node(id).innerGraph();
			if (nested == nullptr)
				continue; // only a node that CONTAINS a graph can ever have been a frontier

			staging.forget(Frontier{&inner, &child, id});
			for (std::size_t i = 0; i < child.childCount(id); ++i)
			{
				forgetInterior(*nested, child.child(id, i), staging);
			}
		}
	}

	// Crossing OUT of a LOOP: publish the fold.
	void Scheduler::exitLoop(const Graph& definition, Evaluation& evaluation, NodeId id, std::size_t iterations)
	{
		const Node& node = definition.node(id);
		const Graph* inner = node.innerGraph();
		const std::optional<IterationPorts> ports = node.iterationPorts();
		const Evaluation& child = evaluation.child(id);
		const NodeId outBoundary = inner->boundaryOutputNode().id();
		NodeEvaluation view = evaluation.node(id);

		// "Could this loop run at all?" and "did the fold break?" are asked HERE, not remembered from
		// the preparation that decided to stop — exitMap's rule, one question in one place. Note the
		// asymmetry ADR-0021 draws: an empty CARRIED output or condition means the fold itself broke
		// and clears everything, while an unpaired output being empty is ordinary per-port emptiness,
		// exactly as it is for a group.
		bool folded = ports.has_value() && evaluation.ready(id);
		if (folded && iterations > 0)
		{
			folded = child.value(PortAddress{outBoundary, ports->condition}).holds<bool>();
			for (const auto& carry : *ports->carries)
			{
				if (!child.hasValue(PortAddress{outBoundary, carry.second}))
					folded = false;
			}
		}

		for (std::size_t o = 0; o < node.outputCount(); ++o)
		{
			const Port& outer = node.output(o);
			PortValue& slot = view.output(outer.id());
			if (!folded)
			{
				slot.clear();
				continue;
			}
			if (outer.id() == ports->report)
			{
				slot = intValue(iterations);
				continue;
			}

			const PortId pin = node.innerPin(outer.id());
			if (pin == PortId{})
			{
				slot.clear(); // mirrors nothing (mid-sync, or a mapping that lost its pin)
				continue;
			}

			// No iteration ran, so there is nothing to copy out: a carried output delivers its seed,
			// and an unpaired one has no last iteration to report.
			slot = (iterations == 0) ? seedValue(node, evaluation, id, *ports, pin)
									 : child.value(PortAddress{outBoundary, pin});
		}

		// The loop's own work IS this crossing, so it goes clean here — the two halves in the order
		// runNode uses, for the same reasons.
		evaluation.clearRecomputeRequest(id);
		evaluation.markComputed(id, node.version());
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
