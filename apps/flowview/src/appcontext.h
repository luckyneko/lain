#pragma once

#include "pinkey.h"
#include "session.h" // what persists between runs (last graph, recents, dialog folder)
#include "undo.h"	 // UndoStack (the graph-document history)

#include <lain/data/value.h>				// Value (pendingBaseline)
#include <lain/flow/serialize/loadresult.h> // EditorData (a value member) + Graph (loadedGraph target)
#include <lain/flow/types.h>				// NodeId

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flowview
{
	class FlowviewApp; // the app delegate: the graph, node palette, and re-run/replace hooks

	// A row in the Issues panel — a validation problem, with an optional node to locate on click.
	struct Issue
	{
		enum class Severity
		{
			Info,	 // a heads-up (an unused output)
			Warning, // something's wrong (a required input unconnected, a rejected connect)
			Error,	 // a structural loss (a load error)
		};
		Severity severity = Severity::Warning;
		std::string message;
		lain::flow::NodeId node; // default (sentinel) = not locatable
	};

	// Replacing the open document — the two ways to do it, both DESTRUCTIVE (New throws the graph away,
	// Open replaces it), so both go through the unsaved-changes guard rather than only New.
	enum class DocumentSwap
	{
		New,
		Open,
	};

	// A requested swap, held while the guard modal is up.
	struct PendingSwap
	{
		DocumentSwap kind = DocumentSwap::New;
		std::filesystem::path path; // Open only: the file to open; empty means "ask with the file dialog"
	};

	// Thumbnail size for the image preview, chosen at runtime via a lain::gui::enumCombo
	// (its labels come from lain::meta::enums).
	enum class PreviewSize
	{
		Small,
		Medium,
		Large,
	};

	// Thumbnail side length (pixels) for a preview size. Shared by the Inspector + Interface panes.
	float previewExtent(PreviewSize size);

	// The shared model the panes read and write — graph-adjacent metadata plus the cross-pane
	// signals (one pane sets a request, another consumes it) and the document / pending-load
	// state. Deliberately plain data + trivial setters: it is not a manager, just the "chairs"
	// the panes sit around. The MainWindow owns one and hands panes a reference. (The gui
	// Context and the preview cache are NOT here — the window owns those and passes them
	// separately, since they are GPU resources, not model state.)
	struct AppContext
	{
		// The app delegate — reached for graph()/nodeFactory()/reevaluate()/replaceGraph(). Set once in
		// MainWindow::onInit (stable for the window's life); null before then.
		FlowviewApp* app = nullptr;

		// Add a catalog node (`key`) to the graph and cascade its grid position so successive adds
		// don't stack; returns its id. Shared by the menu-bar Add and the Nodes palette (both add from
		// the catalog list). The caller sets `edited` so the scene re-runs.
		lain::flow::NodeId addCatalogNode(const std::string& key);

		// --- Graph-adjacent metadata (extra data sitting alongside the graph) ---
		PreviewSize previewSize = PreviewSize::Medium; // thumbnail size (enumCombo-driven)
		int addCounter = 0;							   // palette-added nodes cascade their grid position (menu Add + Nodes palette)
		// The chosen save format per image output pin (the inline dropdown's selection), by format key
		// ("png" / "jpg" / …). Robust to the savable list changing — an entry not (or no longer) in a
		// port's list falls back to that list's first format.
		std::map<PinKey, std::string> saveFormat;

		// --- Cross-pane signals ---
		// Preview routing: a clicked thumbnail (Inspector / Interface) targets an asset; the Preview
		// pane shows it and, on the click frame, brings its tab to front.
		std::optional<PinKey> previewTarget;
		bool activatePreview = false;
		void previewAsset(const PinKey& key)
		{
			previewTarget = key;
			activatePreview = true;
		}

		// Locate: an Issue row asks the Graph pane to centre a node (applied on the next Graph draw,
		// where its drawn position + size are known). Also selects the node + flips to the Graph tab.
		std::optional<lain::flow::NodeId> locateTarget;
		void locateNode(lain::flow::NodeId id); // (touches gui -> defined in the .cpp)

		// Issues: load issues persist until the graph is edited; a rejected connect is a transient row
		// that fades after a few seconds (frame countdown). Live validation isn't stored — it's
		// recomputed from the graph each frame.
		std::vector<Issue> loadIssues;
		std::optional<Issue> recentIssue;
		int recentIssueFrames = 0;
		void noteRejectedConnect()
		{
			recentIssue = Issue{Issue::Severity::Warning, "Rejected connection: incompatible types or a cycle", {}};
			recentIssueFrames = 240; // ~4 s at 60fps
		}

		// --- Document + pending load ---
		// The file the graph was last saved-to / opened-from — plain Save writes here (no dialog); empty
		// until a Save As / Open sets it, and cleared by New.
		std::filesystem::path currentPath;
		// What carries over to the next run: the document to reopen, the Open Recent list, and where the
		// file dialogs should start. Loaded by MainWindow at startup, updated by the menu bar on every
		// Open / Save, and written back on shutdown.
		Session session;
		bool dirty = false; // unsaved changes since the last save / load / new (drives the discard guard)
		// A swap the user asked for while there were unsaved changes: it waits here until the guard
		// modal resolves it (Save / Discard / Cancel).
		std::optional<PendingSwap> pendingSwap; // what to do once confirmed
		bool confirmSwap = false;				// open the modal next frame (the request* helpers set it)

		// Mark that the graph document changed this frame (any topology / param / name edit). Sets the
		// unsaved-changes flag AND asks for an undo snapshot; the snapshot is deferred to end of frame
		// and only taken once no widget is active (so a param drag coalesces into one history entry).
		void markChanged()
		{
			dirty = true;
			pendingSnapshot = true;
		}

		// --- Undo / redo ---
		// The graph-document history (snapshot per committed edit). MainWindow drives it: it pushes a
		// snapshot at end of frame when pendingSnapshot is set and no widget is active, and restores a
		// state through the pending-load path below on Undo/Redo.
		UndoStack undo;
		bool pendingSnapshot = false; // an edit is awaiting its end-of-frame snapshot (coalesces drags)

		// Pending Load: applied at the END of onRender (after every panel drew with the current graph),
		// so replacing the app's graph never dangles the in-flight `graph` reference. The loaded canvas
		// layout is re-applied on the next frame's position-seed pass. An Undo/Redo restore reuses this
		// same path (loadedGraph + pendingLayout + loadRequested).
		bool loadRequested = false;
		std::unique_ptr<lain::flow::Graph> loadedGraph;
		lain::flow::serialize::EditorData pendingLayout;
		// Set alongside a New/Open swap (not an Undo/Redo restore): the freshly-established document to
		// re-baseline the undo history with once the swap is applied. Its presence is what tells the
		// swap handler "this is a new document → reset history" vs "this is a restore → keep history".
		std::optional<lain::data::Value> pendingBaseline;
		// Nodes to re-select after an Undo/Redo swap, as ORDINALS into the graph's node enumeration
		// (nodeIds()) — a restore remaps every NodeId, so the old ids can't be reused, but the ordinal
		// is stable for a structure-preserving edit (e.g. a param drag). Captured before the restore,
		// applied after the swap re-seeds; without it a param undo would drop the canvas selection (and
		// so the selection-driven Inspector). Empty on New/Open, which clear the selection instead.
		std::vector<std::size_t> pendingReselect;
	};
} // namespace flowview
