#pragma once

namespace lain::flow
{
	class Graph;
}

namespace lain::app
{
	class Application; // File ▸ Quit
}

namespace flowview
{
	struct AppContext;

	// The application menu bar (File: New/Open/Save/Save As/Quit; Add ▸ category ▸ kind; View ▸
	// Reset Layout), drawn once per frame at the viewport top, plus the global Cmd/Ctrl shortcuts.
	// Stateless — everything it reads/writes lives in AppContext (document + pending-load state) or
	// is signalled out through draw()'s out-params.
	struct MenuBarPane
	{
		// The menu bar + global shortcuts. An Add creates a node and sets `edited` so the scene
		// re-runs like any edit; View ▸ Reset Layout raises `resetLayout`. Call early in the frame.
		void draw(AppContext& ctx, lain::flow::Graph& graph, lain::app::Application& app, bool& edited, bool& resetLayout);

		// The unsaved-changes confirm modal (opened by New when the document is dirty). Call LATE —
		// after every panel drew — so the modal sits on a clean id stack.
		void drawConfirmModal(AppContext& ctx, lain::flow::Graph& graph);

	private:
		// File actions, shared by the menu items + the shortcuts. New clears to an empty graph (via the
		// dirty guard); Open/Save defer the graph swap to end-of-frame like every graph replacement.
		void newGraph(AppContext& ctx);
		void requestNew(AppContext& ctx);
		void openGraphDialog(AppContext& ctx);
		void saveToCurrentPath(AppContext& ctx, const lain::flow::Graph& graph);
		void saveAsDialog(AppContext& ctx, const lain::flow::Graph& graph);
	};
} // namespace flowview
