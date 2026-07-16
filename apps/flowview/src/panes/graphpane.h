#pragma once

#include "../canvasstyle.h"

#include <typeindex>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	struct AppContext;

	// The node canvas (imnodes) + the "Nodes" add palette. Draws one node per flow node with its ports
	// as pins and edges as links; handles connect / detach / delete / add and the link-drag feedback,
	// the pin tooltip, the minimap, and centring a located node. Owns the canvas-local state: the colour
	// style, the first-frame position seed, and the in-flight link drag. All graph edits accumulate into
	// the caller's `edited` flag (the caller owns the single re-run); it never re-runs the scene itself.
	struct GraphPane
	{
		// Register the canvas colour style + wire Alt-drag panning. Call once from MainWindow::onInit,
		// AFTER the gui Context (hence the imnodes context) exists.
		void init();

		// Draw + edit the canvas and the Nodes palette. Sets `edited` when a node/edge/pin changed, so the
		// caller re-runs the scene and refreshes previews. Reads ctx.pendingLayout (position seed),
		// ctx.locateTarget (centre), and adds via ctx.addCatalogNode / ctx.app->nodeFactory().
		void draw(AppContext& ctx, lain::flow::Graph& graph, bool& edited);

		// Reset the canvas after the graph was replaced (a Load): re-seed positions next frame and drop
		// imnodes' now-stale node/link selection. Called by MainWindow's deferred-load swap.
		void onGraphReplaced();

	private:
		CanvasStyle m_style; // canvas colours + dim state (registered in init)
		bool m_laidOut = false; // seed node positions on the first frame (and after a Load)

		// Link-drag feedback: while a link is dragged, grey every pin that isn't a compatible drop target
		// (opposite direction + same type). Captured on IsLinkStarted (after EndNodeEditor), consumed by
		// the NEXT frame's pin draw — so the grey shows one frame in, invisible mid-drag.
		bool m_linkDragActive = false;
		bool m_linkDragFromOutput = false;			  // the drag source's direction
		std::type_index m_linkDragType{typeid(void)}; // ...and its value type (compatible = same)
	};
} // namespace flowview
