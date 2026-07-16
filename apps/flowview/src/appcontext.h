#pragma once

#include "pinkey.h"

#include <lain/flow/serialize/loadresult.h> // EditorData (a value member) + Graph (loadedGraph target)
#include <lain/flow/types.h>				// NodeId

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

	// Thumbnail size for the image preview, chosen at runtime via a lain::gui::enumCombo
	// (its labels come from lain::meta::enums).
	enum class PreviewSize
	{
		Small,
		Medium,
		Large,
	};

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
		int addCounter = 0; // palette-added nodes cascade their grid position (menu Add + Nodes palette)
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
		bool dirty = false;		 // unsaved changes since the last save / load / new (drives the New guard)
		bool confirmNew = false; // open the unsaved-changes modal next frame (requestNew set it)

		// Pending Load: applied at the END of onRender (after every panel drew with the current graph),
		// so replacing the app's graph never dangles the in-flight `graph` reference. The loaded canvas
		// layout is re-applied on the next frame's position-seed pass.
		bool loadRequested = false;
		std::unique_ptr<lain::flow::Graph> loadedGraph;
		lain::flow::serialize::EditorData pendingLayout;
	};
} // namespace flowview
