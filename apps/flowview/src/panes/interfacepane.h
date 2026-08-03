#pragma once

#include <lain/flow/types.h> // PortAddress (a member of the remove-confirm cluster)

#include <string>

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
	struct InterfacePane
	{
		// Draws the "Interface" window (owns its Begin/End) + the remove-pin confirm modal. Mutates the
		// graph (bind / add / remove pins) and, on a change, re-runs the scene via ctx.app and refreshes
		// the previews.
		void draw(AppContext& ctx, lain::flow::Graph& graph, lain::flow::Evaluation& evaluation, PreviewCache& previews,
				  const ParamEditors& editors);

	private:
		// The "Remove pin?" confirm modal (opened by a "×"); runs edit::removePort on confirm. Returns
		// whether a pin was removed this frame. Drawn after the panel, on a clean id stack.
		bool renderRemoveConfirm(lain::flow::Graph& graph);

		// Remove-pin confirmation: the pin a "×" targeted, its name + incident-link count, and whether
		// the confirm modal is pending (opened on a clean id stack after the panel).
		bool m_removeRequested = false;
		lain::flow::PortAddress m_removeTarget;
		std::string m_removeName;
		int m_removeLinks = 0;
	};
} // namespace flowview
