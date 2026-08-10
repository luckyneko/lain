#pragma once

#include <lain/flow/group.h>
#include <lain/flow/types.h>

#include <filesystem>

namespace lain::data
{
	class Value; // an undo/redo snapshot to restore (applyRestore)
}

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
	struct PendingSwap; // a New / Open awaiting the unsaved-changes guard (appcontext.h)

	// The application menu bar (File: New/Open/Open Recent/Save/Save As/Quit; Add ▸ category ▸ kind;
	// View ▸ Reset Layout), drawn once per frame at the viewport top, plus the global Cmd/Ctrl shortcuts.
	// Stateless — everything it reads/writes lives in AppContext (document + pending-load state) or
	// is signalled out through draw()'s out-params.
	struct MenuBarPane
	{
		// The menu bar + global shortcuts. An Add creates a node and sets `edited` so the scene
		// re-runs like any edit; View ▸ Reset Layout raises `resetLayout`. Call early in the frame.
		void draw(AppContext& ctx, const lain::flow::Graph& graph, lain::app::Application& app, bool& edited,
				  bool& resetLayout);

		// The unsaved-changes confirm modal (opened by New / Open when the document is dirty), which
		// carries out the swap once resolved. Call LATE — after every panel drew — so the modal sits on
		// a clean id stack.
		void drawConfirmModal(AppContext& ctx, const lain::flow::Graph& graph);

		// Load `path` into the scene (deferred to end-of-frame like every graph swap), recording it as
		// the current document + the head of Open Recent. Returns false if nothing loaded — the reason
		// is in ctx.loadIssues and the path is dropped from the recents. Public because the window also
		// calls it at startup, to reopen the graph the last session had open.
		bool openGraphPath(AppContext& ctx, const std::filesystem::path& path);

	private:
		// Add ▸ Linked Group... — pick a template, add a LinkedGroupNode pointed at it (path stored
		// relative to the current document), resolve it immediately so it arrives with its interior and
		// its ports, and report any load issues. Returns whether a node was added.
		bool addLinkedGroup(AppContext& ctx);

		// Group ▸ Edit Template... — open the enclosing linked group's template AS THE DOCUMENT, through
		// the same guarded destructive swap as Open. Editing a template changes every linked group built
		// from it, which is why it is a deliberate act rather than editing in place.
		void editTemplate(AppContext& ctx, const lain::flow::LinkedGroupNode& linked);

		// File ▸ Open Recent — the persisted recent-graph list (+ Clear Menu), greyed out when empty.
		void drawOpenRecent(AppContext& ctx);

		// File actions, shared by the menu items + the shortcuts; the graph swap itself is deferred to
		// end-of-frame like every graph replacement.
		//
		// New and Open are both DESTRUCTIVE (one throws the graph away, the other replaces it), so both
		// are asked for through the guard — request* records the intent, performSwap is what actually
		// carries it out, once there is nothing to lose or the modal has been resolved. Save reports
		// whether it happened, so a cancelled save panel cancels the swap instead of discarding work.
		void newGraph(AppContext& ctx);
		void requestNew(AppContext& ctx);
		void requestOpen(AppContext& ctx, const std::filesystem::path& path = {}); // empty -> the Open... dialog

		// Go back to returnStack[index] — the document Edit Template… was invoked from — through the
		// same guard, since the template may have unsaved edits of its own. Everything from `index` on
		// is consumed, so an outer crumb unwinds several templates at once.
		void returnToDocument(AppContext& ctx, std::size_t index);
		void requestSwap(AppContext& ctx, PendingSwap swap);
		void performSwap(AppContext& ctx, const PendingSwap& swap);
		void openGraphDialog(AppContext& ctx);
		// Save writes the DOCUMENT — the root graph and every level's layout — never the graph the
		// panes happen to be pointed at. `activeGraph` is passed only so the level on screen can have
		// its positions refreshed into the layout tree first.
		const lain::flow::Graph& documentToSave(AppContext& ctx, const lain::flow::Graph& activeGraph);
		bool saveToCurrentPath(AppContext& ctx, const lain::flow::Graph& activeGraph);
		bool saveAsDialog(AppContext& ctx, const lain::flow::Graph& activeGraph);

		// Undo / redo (Edit menu + Ctrl+Z / Ctrl+Shift+Z). Each pulls a document state from the undo
		// history and restores it through the same deferred-swap path as a load; applyRestore does the
		// swap setup without a pendingBaseline, so the history (its cursor already moved) is preserved.
		void undo(AppContext& ctx);
		void redo(AppContext& ctx);
		void applyRestore(AppContext& ctx, const lain::data::Value& state);
	};
} // namespace flowview
