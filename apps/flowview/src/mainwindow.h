#pragma once

#include "appcontext.h"
#include "canvasstyle.h"
#include "panes/interfacepane.h"
#include "panes/issuespane.h"
#include "panes/menubar.h"
#include "panes/previewpane.h"
#include "parameditors.h"
#include "previewcache.h"

#include <lain/app/windowdelegate.h>
#include <lain/gui/context.h>

#include <memory>
#include <typeindex>

namespace flowview
{
	// FlowviewApp, Issue, PreviewSize, and the AppContext shared model live in appcontext.h.

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
		std::unique_ptr<lain::gui::Context> m_guiCtx;
		ParamEditors m_paramEditors; // type-keyed param editors (registered in onInit)
		CanvasStyle m_canvasStyle;	 // canvas colours + dim state (registered in onInit)
		PreviewCache m_previews;	 // one uploaded thumbnail per image port
		MenuBarPane m_menuBar;		 // File / Add / View menu + shortcuts (its own pane)
		IssuesPane m_issues;		 // the Issues panel (validation + load issues)
		PreviewPane m_preview;		 // the Preview panel (clicked asset, full-size)
		InterfacePane m_interface;	 // the Interface panel (graph I/O boundary)
		bool m_laidOut = false;		 // node canvas: seed node positions on the first frame

		// Link-drag feedback (slice C): while a link is dragged, grey every pin that isn't a compatible
		// drop target (opposite direction + same type). Captured on IsLinkStarted (after EndNodeEditor),
		// consumed by the NEXT frame's pin draw — so the grey shows one frame in, invisible mid-drag.
		bool m_linkDragActive = false;
		bool m_linkDragFromOutput = false;			  // the drag source's direction
		std::type_index m_linkDragType{typeid(void)}; // ...and its value type (compatible = same)

		// Docking: seed the default dock layout on the next frame (first run / --reset-layout), and the
		// View ▸ Reset Layout request. Both re-stamp the default (gui::dock*); a saved ~/.flowview/imgui.ini wins.
		bool m_seedLayout = false;
		bool m_resetLayout = false;

		// The shared model the panes read/write: graph-adjacent metadata, cross-pane signals, and the
		// document / pending-load state. The window owns it and hands panes a reference.
		AppContext m_ctx;
	};
} // namespace flowview
