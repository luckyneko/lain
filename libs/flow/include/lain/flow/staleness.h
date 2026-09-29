#pragma once

// The STALE CLOSURE: which nodes of one level show a value that does not reflect a definition
// (ADR-0025). It is the set a run recomputes, and the set a host marks Stale, stated ONCE — the
// scheduler plans from it and a viewer asks it, so the two cannot drift apart.
//
// A node is in the closure when any of these holds:
//   * this evaluation has NO RECORD of it — never computed here (added since, or nothing ran yet);
//   * a recompute has been requested of it;
//   * the definition's version for it differs from the one it was last computed at;
//   * anything INSIDE it is, in any of its child evaluations — every element of a map, since a
//     cancel can stop one part-way;
//   * anything UPSTREAM of it is;
//   * it is this level's boundary input, and the level above says its owner is handing it new
//     values (`boundaryStale` — see reseeds()).
//
// The definition need not be the one the evaluation was prepared against. A gui host asks about the
// DOCUMENT while its panes read an evaluation a run prepared against a CLONE of it (ADR-0025). That
// comparison is legitimate exactly when both are of one LINEAGE, since a clone carries every node's
// version; a definition of another lineage makes the whole level stale rather than throwing —
// prepare() refuses a mispairing because it would ACT on it, while this only reports, and at a child
// a replaced interior is an ordinary edit (a group made local keeps its NodeId and gets a new body).
//
// Staleness crosses levels in BOTH directions, and that is the part a viewer would get wrong on its
// own. Inward: a group whose inputs changed hands its interior new values, so everything downstream
// of that interior's boundary input is stale even though no node in there changed — the level
// above answers it (reseeds()) and the level below is asked with it (`boundaryStale`). Outward: a
// group is stale when anything inside it is, which contains() folds in by itself.

#include "lain/flow/types.h"

#include <set>
#include <vector>

namespace lain::flow
{
	class Evaluation;
	class Graph;

	class StaleClosure
	{
	public:
		// The closure of `definition`'s own level, against what `evaluation` recorded. `boundaryStale`
		// is the answer the level above gave for the node that owns this one (reseeds()); false at the
		// root, where nothing hands the boundary values except a binding, which requests it itself.
		StaleClosure(const Graph& definition, const Evaluation& evaluation, bool boundaryStale = false);

		// Whether `id` shows a value that does not reflect the definition.
		bool contains(NodeId id) const { return m_selected.count(id) != 0; }

		// The closure in topo order — what a run recomputes at this level.
		const std::vector<NodeId>& order() const { return m_order; }

		// Whether the interior of `owner` receives new boundary values: the `boundaryStale` to ask that
		// interior with. It depends on the KIND of interior, which is why it is answered here:
		//   * a group or a map republishes its inputs only when they can have changed — its own record
		//     is stale, or something upstream of it is in the closure (republishes()). One that is
		//     stale only because of its interior hands it nothing new, and its inner nodes upstream of
		//     the change stay current.
		//   * a loop re-folds from its seeds whenever it is in the closure at all, even for an edit
		//     inside its body (ADR-0021), so every pass is recomputed from the boundary.
		// False for a node with no interior.
		bool reseeds(NodeId owner) const { return m_reseeds.count(owner) != 0; }

		// Whether `id` is OWED a compute on its own account — its own record, or anything inside it —
		// as opposed to because of something upstream. What the closure grows from — and never a
		// selection on its own: a node downstream of an edit is owed nothing on its own account and is
		// still owed a compute, which is why even the pull path selects from the closure.
		static bool owed(const Graph& definition, const Evaluation& evaluation, NodeId id);

		// Whether a group or map would republish its inputs into its interior, given the nodes
		// `selected` for recomputing at its level: its own record is stale, or a node feeding it is
		// selected. The one statement of the rule, shared by reseeds() and by the scheduler's plan,
		// which republishes under it — so the two cannot disagree about when an interior receives new
		// values. (The pull path passes the part of the closure in its target's cone — which, for a
		// node in that cone, holds every predecessor the whole closure would, since a cone is closed
		// upstream.)
		static bool republishes(const Graph& definition, const Evaluation& evaluation, NodeId id,
								const std::set<NodeId>& selected);

	private:
		// Whether `evaluation`'s records may be compared against `definition` at all (same lineage, or
		// none recorded yet), and `id`'s own record against it. Members only because they read the
		// evaluation's private records — this class is its friend, and no host needs either.
		static bool pairs(const Graph& definition, const Evaluation& evaluation);
		static bool recordStale(const Graph& definition, const Evaluation& evaluation, NodeId id);

		std::vector<NodeId> m_order;
		std::set<NodeId> m_selected;
		std::set<NodeId> m_reseeds;
	};
} // namespace lain::flow
