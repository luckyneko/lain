#include "mainwindow.h"

#include "flowviewapp.h"
#include "session.h" // loadSession / saveSession (reopen the last graph, restore the dialog folder)

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/core/paths.h> // core::configDir (~/.flowview for the layout ini)
#include <lain/flow/graph.h>
#include <lain/gui/dialogs.h> // lastDirectory / setLastDirectory (the persisted dialog folder)
#include <lain/gui/dock.h>	  // the docking seam (no imgui_internal in the app)

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace flowview
{
	using namespace lain;

	bool MainWindow::onInit(app::Window& window)
	{
		// Dock layout persists to ~/.flowview/imgui.ini. Seed the default arrangement on first run (no
		// saved ini) or when --reset-layout is given; otherwise the saved layout is restored.
		FlowviewApp& appDelegate = window.app().getDelegate<FlowviewApp>();
		m_ctx.app = &appDelegate; // the shared model reaches the graph / palette / re-run hooks through this
		const std::filesystem::path iniPath = core::configDir("flowview") / "imgui.ini";
		m_seedLayout = appDelegate.resetLayout() || !std::filesystem::exists(iniPath);
		gui::ContextConfig guiConfig;
		guiConfig.iniFilename = iniPath.string();
		guiConfig.docking = true;
		m_guiCtx = std::make_unique<gui::Context>(window.app(), window, guiConfig);
		registerBuiltinParamEditors(m_paramEditors);
		m_canvas.init(); // canvas colour style + Alt-drag panning (needs the imnodes context above)

		// What carried over from the last run: put the file dialogs back where they were, and reopen the
		// graph that was open — the load is deferred to the end of the first frame like every graph swap,
		// so the blank scene FlowviewApp built stands in until then (and stays, if the file is gone).
		// --example asks for the demo scene explicitly, so it wins over the restore.
		m_ctx.session = loadSession();
		gui::setLastDirectory(m_ctx.session.lastDialogDir);
		if (!appDelegate.useExample() && !m_ctx.session.lastGraph.empty())
			m_menuBar.openGraphPath(m_ctx, m_ctx.session.lastGraph);
		return true;
	}

	// Stamp the default dock arrangement (via the gui docking seam). Right column (Inspector/Nodes over
	// Interface) full-height; the left splits Graph/Preview over Issues on its own ratio — so the two
	// columns' horizontal splitters are independent. Window names must match the panels' gui::Begin.
	static void buildDefaultLayout(gui::DockNode dock)
	{
		gui::dockReset(dock);
		const auto [rightCol, leftCol] = gui::dockSplit(dock, gui::DockDir::Right, 0.28f); // right column, full height
		const auto [issues, graphArea] = gui::dockSplit(leftCol, gui::DockDir::Down, 0.22f); // Issues under Graph/Preview
		const auto [ioArea, inspectorArea] = gui::dockSplit(rightCol, gui::DockDir::Down, 0.5f); // Interface under Inspector/Nodes

		gui::dockWindow(graphArea, "Graph");
		gui::dockWindow(graphArea, "Preview"); // tab with Graph
		gui::dockWindow(issues, "Issues");
		gui::dockWindow(inspectorArea, "Inspector");
		gui::dockWindow(inspectorArea, "Nodes"); // tab with Inspector
		gui::dockWindow(ioArea, "Interface");
		gui::dockFinish(dock);
	}

	void MainWindow::onRender(app::Window& window, const app::TimeState&)
	{
		FlowviewApp& appDelegate = window.app().getDelegate<FlowviewApp>();
		flow::Graph& graph = appDelegate.graph(); // mutated by the canvas below

		m_guiCtx->newFrame();

		// A full-viewport dockspace (below the menu bar) so the panels tile with draggable splitters and
		// can't get lost behind the Graph. On first run / reset, stamp the default arrangement.
		const gui::DockNode dock = gui::dockSpaceOverViewport();
		if (m_seedLayout || m_resetLayout)
		{
			buildDefaultLayout(dock);
			m_seedLayout = false;
			m_resetLayout = false;
		}

		// The node canvas is drawn (and edited) BEFORE the inspector panel, so a node
		// deletion takes effect before the panel reads the graph and re-registers its
		// texture preview — the panel never references a just-freed node's texture. All its
		// edits accumulate into `edited`, alongside the menu-bar Add below.
		bool edited = false;
		m_canvas.draw(m_ctx, graph, edited);

		// The application menu bar (viewport-top; ImGui places it there regardless of call order). Its
		// Add feeds the same `edited` flag as the canvas, so a menu Add Node re-runs the scene like any edit.
		m_menuBar.draw(m_ctx, graph, window.app(), edited, m_resetLayout);

		// A topology edit re-runs the scene, so the previews need refreshing (recomputed
		// images, added/removed ports). Mark them dirty; refreshIfDirty below upserts in
		// place where it can. (Deferred, not per-frame — acm::Texture::upload is a stalling
		// synchronous submit.)
		if (edited)
		{
			appDelegate.reevaluate();
			m_previews.markDirty();
			m_ctx.dirty = true; // a topology edit -> unsaved changes
			m_ctx.loadIssues.clear();
		}

		m_previews.refreshIfDirty(graph, *m_guiCtx);

		// The per-node Inspector — reads (and param-edits) the now post-edit graph.
		m_inspector.draw(m_ctx, graph, m_previews, m_paramEditors);

		// The graph's I/O boundary — the host-binding surface (bind inputs, save outputs).
		m_interface.draw(m_ctx, graph, m_previews, m_paramEditors);

		// Preview + Issues panels — docked windows the default layout tiles alongside the Graph.
		m_preview.draw(m_ctx, graph, m_previews);
		m_issues.draw(m_ctx, graph);

		// The unsaved-changes guard for New (opened by requestNew when m_ctx.dirty).
		m_menuBar.drawConfirmModal(m_ctx, graph);

		// Apply a pending Load now — every panel has drawn with the current graph, so swapping it
		// here (not at the button) can't dangle the `graph` reference used above. Next frame re-seeds
		// positions from the loaded layout.
		if (m_ctx.loadRequested)
		{
			appDelegate.replaceGraph(std::move(m_ctx.loadedGraph));
			m_ctx.loadRequested = false;
			m_previews.clear(); // the old graph's cached thumbnails are gone
			m_previews.markDirty();
			m_canvas.onGraphReplaced(); // re-seed positions next frame + drop stale canvas selection
		}

		window.renderer().render([&](acm::CommandBuffer cmd, uint32_t)
								 { m_guiCtx->render(cmd); });
	}

	void MainWindow::onShutdown(app::Window&)
	{
		// Persist the dialog folder as it ended up — every dialog (graph, image bind, image save) feeds
		// the one gui-side "last folder", so this is the only place that needs to read it. The document
		// and recents were already written as they changed, so a crash loses at most this.
		m_ctx.session.lastDialogDir = gui::lastDirectory();
		saveSession(m_ctx.session);

		m_previews.clear(); // release the preview descriptors while the ImGui backend lives
		m_guiCtx.reset();	// then destroy the backend, before the device tears down
	}
} // namespace flowview
