#pragma once

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	struct AppContext;

	// The Issues panel: live validation of the current graph (recomputed each frame) plus the
	// persisted load issues and a transient rejected-connect. Each row is click-to-locate (via
	// AppContext::locateNode). Stateless — the persisted issues live in AppContext.
	struct IssuesPane
	{
		// Draws the "Issues" window (owns its Begin/End). ctx is mutable — the transient
		// rejected-connect row counts down here, and a clicked row calls ctx.locateNode.
		void draw(AppContext& ctx, const lain::flow::Graph& graph);
	};
} // namespace flowview
