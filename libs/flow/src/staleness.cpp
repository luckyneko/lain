#include "lain/flow/staleness.h"

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"

#include <map>

namespace lain::flow
{
	// Whether `evaluation`'s records may be compared against `definition` at all: both of one
	// lineage, or the evaluation has not been prepared against anything yet (a fresh one, or an empty
	// published copy), in which case it simply has no records and every node is stale.
	bool StaleClosure::pairs(const Graph& definition, const Evaluation& evaluation)
	{
		return evaluation.m_lineage == Lineage{} || evaluation.m_lineage == definition.lineage();
	}

	// `id`'s OWN record, against `definition`'s version of it — not anything inside it, and not
	// anything upstream. No record at all is stale: the node was never computed in this evaluation.
	// (Inside a run that never happens, since prepare() makes a record for every node first; a host
	// asking about the document meets it whenever a node was added since the last publication.)
	bool StaleClosure::recordStale(const Graph& definition, const Evaluation& evaluation, NodeId id)
	{
		const Evaluation::NodeState* state = evaluation.state(id);
		if (state == nullptr)
			return true;
		return state->recomputeRequested || definition.node(id).version() != state->computedAt;
	}

	bool StaleClosure::owed(const Graph& definition, const Evaluation& evaluation, NodeId id)
	{
		if (recordStale(definition, evaluation, id))
			return true;

		const Graph* inner = definition.node(id).innerGraph();
		if (inner == nullptr)
			return false;

		// EVERY child, not only the first. A map's children share one definition and are run together,
		// so an edit inside it shows in all of them alike — but a cancelled run can stop part-way
		// through a map, leaving element 2 owed while element 0 finished, and the gather still runs (it
		// is a crossing). Asking child 0 alone would call that map clean and keep its mixed collection.
		for (std::size_t i = 0; i < evaluation.childCount(id); ++i)
		{
			const Evaluation& child = evaluation.child(id, i);
			// A child of another lineage belongs to an interior that has since been replaced, so
			// nothing it recorded describes this one.
			if (!pairs(*inner, child))
				return true;
			for (const NodeId innerId : inner->nodeIds())
			{
				if (owed(*inner, child, innerId))
					return true;
			}
		}
		return false;
	}

	bool StaleClosure::republishes(const Graph& definition, const Evaluation& evaluation, NodeId id,
								   const std::set<NodeId>& selected)
	{
		if (!pairs(definition, evaluation) || recordStale(definition, evaluation, id))
			return true;
		for (const Graph::Edge& e : definition.edges())
		{
			if (e.to.node == id && selected.count(e.from.node) != 0)
				return true;
		}
		return false;
	}

	// topoOrder() is sources-first, so a node's predecessors are already decided by the time it is
	// visited — a node is selected iff it is owed on its own account, is the boundary a reseeded
	// interior receives values through, or has a selected predecessor.
	StaleClosure::StaleClosure(const Graph& definition, const Evaluation& evaluation, bool boundaryStale)
	{
		// Records of another version history describe nothing here: all of it is stale, and every
		// interior is handed values it has not seen.
		const bool paired = pairs(definition, evaluation);
		const NodeId boundary = definition.boundaryInputNode().id();

		std::map<NodeId, std::vector<NodeId>> predecessors;
		for (const Graph::Edge& e : definition.edges())
			predecessors[e.to.node].push_back(e.from.node);

		for (const NodeId id : definition.topoOrder())
		{
			bool selected = !paired || (boundaryStale && id == boundary) || owed(definition, evaluation, id);
			if (!selected)
			{
				const auto it = predecessors.find(id);
				if (it != predecessors.end())
				{
					for (const NodeId pred : it->second)
					{
						if (m_selected.count(pred) != 0)
						{
							selected = true;
							break;
						}
					}
				}
			}

			// Decided BEFORE this node joins the selection, and it does not matter that it has not:
			// what republishing asks about is this node's own record and its PREDECESSORS, every one
			// of which has already been decided.
			const Node& node = definition.node(id);
			if (node.innerGraph() != nullptr)
			{
				const bool reseed = (node.interiorEvaluation() == InteriorEvaluation::PerIteration)
										? selected
										: republishes(definition, evaluation, id, m_selected);
				if (reseed)
					m_reseeds.insert(id);
			}

			if (selected)
			{
				m_selected.insert(id);
				m_order.push_back(id);
			}
		}
	}
} // namespace lain::flow
