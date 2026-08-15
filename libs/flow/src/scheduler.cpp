#include "lain/flow/scheduler.h"

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h" // Graph + the boundary nodes a group step crosses through
#include "lain/flow/group.h" // MapNode — the scheduler asks whether a node maps, not which class it is

#include <lain/task/task.h>

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

	// Flatten one level into the plan, recursing through anything that contains a graph.
	//
	// A plain node becomes one step that both consumes (populateInputs) and produces. A group
	// becomes THREE parts — entry, its inner graph's steps, exit — so its consuming end is the
	// entry step and its producing end is the exit step. A MAP becomes its children's steps plus a
	// gathering exit, or is deferred to the next stage. Wiring this level's edges between those ends
	// is what stitches the levels into a single DAG.
	void Scheduler::expand(const Graph& definition, Evaluation& evaluation, const std::vector<NodeId>& order, Plan& plan,
						   const PreparedMaps& prepared)
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

		// Has this map already been prepared during this invocation? That is what bounds the staging
		// loop: a map is deferred at most once, so the stage count cannot exceed the number of maps.
		const auto alreadyPrepared = [&](NodeId id)
		{
			for (const Frontier& f : prepared)
			{
				if (f.definition == &definition && f.evaluation == &evaluation && f.node == id)
					return true;
			}
			return false;
		};

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
			const bool isMap = node.evaluatesPerElement() && inner != nullptr;
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
			// The `prepared` check is what makes the SECOND stage expand it instead of deferring it
			// again; preparation also REQUESTS this map's recompute, so that it — and everything
			// downstream of it — is still selected when the plan is rebuilt.
			if (isMap)
			{
				if (publish && !alreadyPrepared(id))
				{
					deferred.insert(id);
					plan.frontiers.push_back(Frontier{&definition, &evaluation, id});
					continue;
				}

				// N children, all sharing one definition — the target workload, finally literal.
				const std::size_t count = evaluation.childCount(id);
				const std::size_t begin = plan.steps.size();
				for (std::size_t i = 0; i < count; ++i)
					expand(*inner, evaluation.child(id, i), runOrder(*inner, evaluation.child(id, i)), plan, prepared);
				const std::size_t end = plan.steps.size();

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
			expand(*inner, child, runOrder(*inner, child), plan, prepared);
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

	Scheduler::Plan Scheduler::buildRunPlan(const Graph& definition, Evaluation& evaluation, const PreparedMaps& prepared)
	{
		Plan plan;
		expand(definition, evaluation, runOrder(definition, evaluation), plan, prepared);
		return plan;
	}

	Scheduler::Plan Scheduler::buildEvalPlan(const Graph& definition, Evaluation& evaluation, NodeId target,
											 const PreparedMaps& prepared)
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
		expand(definition, evaluation, order, plan, prepared);
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
		// Every map prepared so far in this invocation. A map is deferred at most once, so this also
		// bounds the loop: at most one stage per map, plus the final one.
		PreparedMaps prepared;

		while (true)
		{
			const Plan plan = (mode == Mode::Push)
								  ? buildRunPlan(definition, evaluation, prepared)
								  : buildEvalPlan(definition, evaluation, target, prepared);

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
				prepareMap(frontier);
				prepared.push_back(frontier);
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
