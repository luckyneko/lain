#pragma once

#include "canvasstyle.h"
#include "parameditors.h"
#include "pinkey.h"
#include "previewcache.h"

#include <lain/app/windowdelegate.h>
#include <lain/flow/serialize/loadresult.h> // EditorData + Graph (complete, for the m_loadedGraph member)
#include <lain/flow/types.h>				 // PortIndex
#include <lain/gui/context.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace lain::flow
{
	class Graph;
}

namespace lain::app
{
	class Application; // File ▸ Quit
}

namespace lain::image
{
	class Image;
}

namespace flowview
{
	class FlowviewApp; // the delegate whose graph the Interface panel binds

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


	// The app's main window: it drives every pane (Graph canvas, Inspector, Interface, Preview,
	// Issues, menu bar) inside one docking layout. Owns its GUI resources (the lain::gui Context +
	// the per-port preview cache); it holds no app/graph state. Each hook reaches what it needs
	// from its Window argument — window.app() for the Application, and
	// window.app().getDelegate<FlowviewApp>().graph() for the scene it draws. The Inspector pane
	// shows each node's ports as text and a lain::image::Image output as a live thumbnail. The
	// Vulkan/ImGui plumbing of that thumbnail lives in lain::gui — this adapter only holds
	// gui::Texture handles keyed by pin. ImGui is single-threaded — every call here is on the
	// main/render thread.
	class MainWindow : public lain::app::WindowDelegate
	{
	public:
		bool onInit(lain::app::Window& window) override;
		void onRender(lain::app::Window& window, const lain::app::TimeState& time) override;
		void onShutdown(lain::app::Window& window) override;

	private:
		// The application menu bar (File: New/Open/Save/Save As/Quit; Add ▸ category ▸ kind), drawn once
		// per frame at the viewport top, plus the global Cmd/Ctrl shortcuts. Menu edits feed the shared
		// `edited` flag so the scene re-runs like any edit.
		void renderMenuBar(lain::flow::Graph& graph, FlowviewApp& appDelegate, lain::app::Application& app, bool& edited);
		// File actions (shared by the menu items + the shortcuts). New clears to an empty graph; Open
		// defers the graph swap to end-of-frame (m_loadRequested) like every graph replacement; Save
		// writes to the remembered path (m_currentPath), falling back to Save As when none is set yet.
		void newGraph();
		void openGraphDialog(FlowviewApp& appDelegate);
		void saveToCurrentPath(const lain::flow::Graph& graph, FlowviewApp& appDelegate);
		void saveAsDialog(const lain::flow::Graph& graph, FlowviewApp& appDelegate);
		// New guards against silent data loss: if there are unsaved changes it opens a Save/Discard/Cancel
		// modal (renderNewConfirm) instead of clearing straight away; otherwise it clears immediately.
		void requestNew();
		void renderNewConfirm(lain::flow::Graph& graph, FlowviewApp& appDelegate);

		// The graph's I/O boundary as a panel (Inputs: Bind file…; Outputs: thumbnail +
		// Save…), driven by Graph::boundaryInputs()/outputs() — the same seam the cli binds
		// through. The host-binding surface, separate from the per-node inspector.
		void renderInterfacePanel(FlowviewApp& appDelegate);

		// The format dropdown + Save… for one image, shared by the inspector's output ports and
		// the Interface panel's outputs. `key` scopes the per-pin remembered format choice.
		void renderImageSave(const PinKey& key, const lain::image::Image& img);

		// The "Remove pin?" confirm modal (opened by a "×"); runs edit::removePort on confirm.
		// Returns whether a pin was removed this frame.
		bool renderRemoveConfirm(lain::flow::Graph& graph);

		// The Preview pane (tabbed with the Graph): the asset that was last clicked (a thumbnail
		// anywhere) shown fit-to-pane; a hint when nothing is targeted or the source is gone.
		void renderPreview(const lain::flow::Graph& graph);
		// Route an asset (an image port) to the Preview pane and bring its tab to front. Called from a
		// clicked thumbnail in the Inspector / Interface.
		void previewAsset(const PinKey& key)
		{
			m_previewTarget = key;
			m_activatePreview = true;
		}

		// The Issues panel: live validation of the current graph (recomputed each frame) plus persisted
		// load issues and a transient rejected-connect, each row click-to-locate.
		void renderIssues(const lain::flow::Graph& graph);
		// Select + frame a node on the canvas (from an Issue row) and bring the Graph tab to front.
		void locateNode(lain::flow::NodeId id);

		std::unique_ptr<lain::gui::Context> m_guiCtx;
		ParamEditors m_paramEditors; // type-keyed param editors (registered in onInit)
		CanvasStyle m_canvasStyle;	 // canvas colours + dim state (registered in onInit)
		PreviewCache m_previews;	 // one uploaded thumbnail per image port
		bool m_laidOut = false;		 // node canvas: seed node positions on the first frame
		int m_addCounter = 0;		 // palette-added nodes cascade their position

		// Link-drag feedback (slice C): while a link is dragged, grey every pin that isn't a compatible
		// drop target (opposite direction + same type). Captured on IsLinkStarted (after EndNodeEditor),
		// consumed by the NEXT frame's pin draw — so the grey shows one frame in, invisible mid-drag.
		bool m_linkDragActive = false;
		bool m_linkDragFromOutput = false;					// the drag source's direction
		std::type_index m_linkDragType{typeid(void)};		// ...and its value type (compatible = same)
		PreviewSize m_previewSize = PreviewSize::Medium; // thumbnail size (enumCombo-driven)

		// The chosen save format per image output pin (the inline dropdown's selection), by format
		// key ("png" / "jpg" / …). Robust to the savable list changing — an entry not (or no
		// longer) in a port's list falls back to that list's first format.
		std::map<PinKey, std::string> m_saveFormat;

		// Remove-pin confirmation: the pin a "×" targeted, its name + incident-link count, and
		// whether the confirm modal is pending (opened on a clean id stack after the panel).
		bool m_removeRequested = false;
		lain::flow::PortAddress m_removeTarget;
		std::string m_removeName;
		int m_removeLinks = 0;

		// Pending Load: applied at the END of onRender (after every panel drew with the current
		// graph), so replacing the app's graph never dangles the in-flight `graph` reference. The
		// loaded canvas layout is re-applied on the next frame's position-seed pass.
		bool m_loadRequested = false;
		std::unique_ptr<lain::flow::Graph> m_loadedGraph;
		lain::flow::serialize::EditorData m_pendingLayout;

		// The file the graph was last saved-to / opened-from — plain Save writes here (no dialog); empty
		// until a Save As / Open sets it, and cleared by New.
		std::filesystem::path m_currentPath;
		bool m_dirty = false;	  // unsaved changes since the last save / load / new (drives the New guard)
		bool m_confirmNew = false; // open the unsaved-changes modal next frame (requestNew set it)

		// Docking: seed the default dock layout on the next frame (first run / --reset-layout), and the
		// View ▸ Reset Layout request. Both re-stamp the default (gui::dock*); a saved ~/.flowview/imgui.ini wins.
		bool m_seedLayout = false;
		bool m_resetLayout = false;

		// The asset shown in the Preview pane (an image port, by stable key), or none. m_activatePreview
		// brings the Preview tab to front on the frame a thumbnail is clicked.
		std::optional<PinKey> m_previewTarget;
		bool m_activatePreview = false;

		// Issues panel state: load issues persist until the graph is edited; a rejected connect is a
		// transient row that fades after a few seconds (frame countdown). Live validation isn't stored —
		// it's recomputed from the graph each frame.
		std::vector<Issue> m_loadIssues;
		std::optional<Issue> m_recentIssue;
		int m_recentIssueFrames = 0;

		// A node to centre on the canvas (from a click-to-locate); applied on the next Graph draw, which
		// is where its drawn position + size are known.
		std::optional<lain::flow::NodeId> m_locateTarget;
	};
} // namespace flowview
