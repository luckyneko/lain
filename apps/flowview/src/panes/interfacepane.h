#pragma once

#include <lain/flow/types.h> // PortAddress (a member of the remove-confirm cluster)

#include <string>
#include <vector>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace flowview
{
	struct AppContext;
	class PreviewCache;
	class ParamEditors;

	// The graph's I/O boundary as a panel (Inputs: editable name, thumbnail, Bind file…, ×, + add pin;
	// Outputs: name, result thumbnail, Save…, ×, + add pin), driven by Graph::boundaryInputs()/outputs()
	// — the same seam the cli binds through. The host-binding surface, separate from the per-node
	// Inspector. Owns the remove-pin confirmation cluster (its only local state).
	//
	// Inside a LOOP it also owns the CARRIES: the pairing is the one thing about a loop's interior
	// that is not derivable from it (ADR-0021), so it is the one thing this panel has to state and
	// to author. Without the gesture a loop would be addable and unauthorable.
	struct InterfacePane
	{
		// Draws the "Interface" window (owns its Begin/End) + the remove-pin confirm modal. Mutates the
		// graph (bind / add / remove pins) and, on a change, re-runs the scene via ctx.app and refreshes
		// the previews.
		void draw(AppContext& ctx, const lain::flow::Graph& graph, lain::flow::Graph* editable,
				  lain::flow::Evaluation& evaluation, PreviewCache& previews, const ParamEditors& editors);

	private:
		// The "Remove pin?" confirm modal (opened by a "×"); runs edit::removePort on confirm. Returns
		// whether anything was removed this frame. Drawn after the panel, on a clean id stack.
		bool renderRemoveConfirm(lain::flow::Graph* editable);

		// Remove confirmation: the pins a "×" targeted, what to call them, their summed incident-link
		// count, and whether the modal is pending (opened on a clean id stack after the panel).
		//
		// A VECTOR because removing a CARRY removes both of its pins — the atomic inverse of the
		// paired add, and the reason addCarry vets both sides before touching either. One target is
		// simply the one-element case.
		bool m_removeRequested = false;
		std::vector<lain::flow::PortAddress> m_removeTargets;
		std::string m_removeLabel;
		int m_removeLinks = 0;
	};
} // namespace flowview
