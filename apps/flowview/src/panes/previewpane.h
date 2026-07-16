#pragma once

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	struct AppContext;
	class PreviewCache;

	// The Preview pane (tabbed with the Graph): the asset last clicked (a thumbnail anywhere, routed
	// via AppContext::previewAsset) shown fit-to-pane; a hint when nothing is targeted or the source
	// is gone. Stateless — the target + activate signal live in AppContext, the thumbnail in the cache.
	struct PreviewPane
	{
		// Draws the "Preview" window (owns its Begin/End) and, on the frame a thumbnail was clicked,
		// brings the Preview tab forward. ctx is mutable — it consumes the activate signal and drops a
		// stale target.
		void draw(AppContext& ctx, const lain::flow::Graph& graph, const PreviewCache& previews);
	};
} // namespace flowview
