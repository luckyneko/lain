#pragma once

#include "../pinkey.h"

#include <memory>
#include <optional>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace lain::gui
{
	class Context;
}

namespace flowview
{
	struct AppContext;
	class PreviewCache;
	class ValueView;
	class ValueViews;

	// The Preview pane (tabbed with the Graph): the asset last clicked (a thumbnail anywhere, routed
	// via AppContext::previewAsset) shown large; a hint when nothing is targeted or the source is gone.
	//
	// The pane owns the target's identity and its own resolution; WHAT a value looks like is the
	// ValueViews registry's — an image gets zoom and pan, a frame sequence gets a player. It holds one
	// live ValueView, remade whenever the target changes, because a view owns state (zoom, playback
	// position, a decoded frame) that belongs to what is on screen and to nothing else.
	struct PreviewPane
	{
		PreviewPane();
		~PreviewPane(); // out of line: m_view holds an incomplete ValueView above

		// Draws the "Preview" window (owns its Begin/End) and, on the frame a thumbnail was clicked,
		// brings the Preview tab forward. ctx is mutable — it consumes the activate signal and drops a
		// stale target. `gui` is handed on to the view, for one that uploads images of its own.
		void draw(AppContext& ctx, const lain::flow::Graph& graph, const lain::flow::Evaluation& evaluation,
				  const PreviewCache& previews, const ValueViews& views, lain::gui::Context& gui);

		// Drop the live view — and with it any GPU texture it holds. MUST run while the gui Context's
		// ImGui backend is still alive (gui::Texture reclaims its descriptor there), which is why
		// MainWindow::onShutdown calls it beside PreviewCache::clear rather than leaving it to this
		// pane's destructor, which runs after the Context is gone.
		void releaseView();

	private:
		// The window's contents (the caller owns Begin/End). A member, not a free function: it reads
		// and writes the view state below.
		void drawContents(AppContext& ctx, const lain::flow::Graph& graph, const lain::flow::Evaluation& evaluation,
						  const PreviewCache& previews, const ValueViews& views, lain::gui::Context& gui);

		std::unique_ptr<ValueView> m_view; // the view for m_shownKey's type, or null
		std::optional<PinKey> m_shownKey;  // which asset m_view belongs to
	};
} // namespace flowview
