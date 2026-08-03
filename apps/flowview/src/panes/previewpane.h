#pragma once

#include "../pinkey.h"

#include <optional>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

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
		void draw(AppContext& ctx, const lain::flow::Graph& graph, const lain::flow::Evaluation& evaluation, const PreviewCache& previews);

	private:
		// The window's contents (the caller owns Begin/End). A member now, not a free function: it
		// reads and writes the view state below.
		void drawContents(AppContext& ctx, const lain::flow::Graph& graph, const lain::flow::Evaluation& evaluation, const PreviewCache& previews);

		// View state. `fitMode` is a MODE, not a zoom value: while it holds, the view re-fits as the
		// pane is resized, and it releases the moment the user zooms deliberately (Fit re-enters it).
		// Zoom resets to Fit when the previewed asset changes — carrying a 12x zoom onto a different
		// image would be jarring, and the new one may be a different size entirely.
		float m_zoom = 1.0f;
		bool m_fitMode = true;
		std::optional<PinKey> m_shownKey; // which asset m_zoom belongs to
	};
} // namespace flowview
