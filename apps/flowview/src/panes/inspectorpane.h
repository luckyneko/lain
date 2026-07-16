#pragma once

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	struct AppContext;
	class PreviewCache;
	class ParamEditors;

	// The per-node Inspector: selection-driven (it inspects only the node(s) selected on the canvas,
	// walked in topo order). Shows each node's editable params (by type, via the registry) and its
	// ports as text, with a ready image port previewed as a thumbnail + Save… widget. Boundary nodes
	// defer to the Interface panel. Stateless — everything it touches lives in AppContext / the caller.
	struct InspectorPane
	{
		// Draws the "Inspector" window (owns its Begin/End). Mutates the graph (param edits) and, on a
		// change, re-runs the scene via ctx.app and refreshes the previews.
		void draw(AppContext& ctx, lain::flow::Graph& graph, PreviewCache& previews, const ParamEditors& editors);
	};
} // namespace flowview
