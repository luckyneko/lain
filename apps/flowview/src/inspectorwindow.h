#pragma once

#include "canvasstyle.h"
#include "parameditors.h"

#include <lain/app/windowdelegate.h>
#include <lain/flow/serialize/loadresult.h> // EditorData + Graph (complete, for the m_loadedGraph member)
#include <lain/flow/types.h>				 // PortIndex
#include <lain/gui/context.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <typeindex>

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
	// Thumbnail size for the image preview, chosen at runtime via a lain::gui::enumCombo
	// (its labels come from lain::meta::enums).
	enum class PreviewSize
	{
		Small,
		Medium,
		Large,
	};

	// Identifies one port's preview by its stable logical position (node + direction +
	// index) — a key that survives recomputes but not node deletion. No GPU/ImGui types.
	struct PinKey
	{
		std::uint64_t node;
		bool output;
		lain::flow::PortId port; // stable id, so a preview survives sibling pins changing

		bool operator<(const PinKey& o) const
		{
			if (node != o.node)
				return node < o.node;
			if (output != o.output)
				return output < o.output;
			return port < o.port;
		}
	};

	// Per-window ImGui inspector. Owns only its GUI resources (the lain::gui Context + the
	// per-port preview cache); it holds no app/graph state. Each hook reaches what it needs
	// from its Window argument — window.app() for the Application, and
	// window.app().getDelegate<FlowviewApp>().graph() for the scene it draws. The panel
	// shows each node's ports as text and a lain::image::Image output as a live thumbnail.
	// The Vulkan/ImGui plumbing of that thumbnail lives in lain::gui — this adapter only
	// holds gui::Texture handles keyed by pin. ImGui is single-threaded — every call here
	// is on the main/render thread.
	class InspectorWindow : public lain::app::WindowDelegate
	{
	public:
		bool onInit(lain::app::Window& window) override;
		void onRender(lain::app::Window& window, const lain::app::TimeState& time) override;
		void onShutdown(lain::app::Window& window) override;

	private:
		// Upsert a preview per ready image port (upload in place when the size/format
		// matches, else recreate) and prune previews whose port is gone. Runs only when the
		// scene may have changed (first frame + after an edit) — acm::Texture::upload is a
		// synchronous submit, so it must not run every frame.
		void refreshPreviews(const lain::flow::Graph& graph);

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

		std::unique_ptr<lain::gui::Context> m_guiCtx;
		ParamEditors m_paramEditors;					 // type-keyed param editors (registered in onInit)
		CanvasStyle m_canvasStyle;						 // canvas colours + dim state (registered in onInit)
		std::map<PinKey, lain::gui::Texture> m_previews; // one uploaded thumbnail per image port
		bool m_previewsDirty = true;					 // rebuild previews on the next frame (init + after edits)
		bool m_laidOut = false;							 // node canvas: seed node positions on the first frame
		int m_addCounter = 0;							 // palette-added nodes cascade their position

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
	};
} // namespace flowview
