#include "mainwindow.h"

#include "flowviewapp.h"
#include "graphio.h"		   // snapshotGraph (undo snapshots)
#include "panes/canvasstate.h" // collectLayout (canvas positions for a snapshot)
#include "session.h"		   // loadSession / saveSession (reopen the last graph, restore the dialog folder)

#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/core/paths.h> // core::configDir (~/.flowview for the layout ini)
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/gui/dialogs.h> // lastDirectory / setLastDirectory (the persisted dialog folder)
#include <lain/gui/dock.h>	  // the docking seam (no imgui_internal in the app)
#include <lain/gui/gui.h>	  // IsAnyItemActive (coalesce a drag into one undo snapshot)
#include <lain/gui/nodes.h>	  // SelectNode (re-select after an undo/redo restore)

#include <archimedes/archimedes.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
		const auto [rightCol, leftCol] = gui::dockSplit(dock, gui::DockDir::Right, 0.28f);		 // right column, full height
		const auto [issues, graphArea] = gui::dockSplit(leftCol, gui::DockDir::Down, 0.22f);	 // Issues under Graph/Preview
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

		// Consume a navigation requested LAST frame (a breadcrumb click, a double-click descent) before
		// resolving the path, so the canvas seeds positions for the level it is about to draw. Doing
		// this inside the canvas would consume a flag raised during its own draw — seeding the level
		// being left, and leaving the one being entered to inherit stale positions by node id.
		if (m_ctx.pathChanged)
		{
			m_canvas.onNavigated();
			m_ctx.pathChanged = false;

			// Drop the previews with it. Not merely tidiness: a PinKey is {node id, direction, port
			// id} with NO level in it, and node ids repeat across levels — so a stale entry from the
			// level we just left can be found by a same-numbered pin here, and the panes would show
			// another graph's image as if it were this one's. Only one level is ever on screen, so
			// keeping the other's textures buys nothing and risks exactly that.
			m_previews.clear();
			m_previews.markDirty();
			m_ctx.previewTarget.reset(); // the asset being previewed belonged to that other level
		}

		// The ACTIVE graph — the root, or whatever group the user has descended into. Resolved once
		// here and handed to every pane, so navigating retargets the canvas, Inspector, Preview and
		// Issues together. resolvePath truncates a path that no longer resolves, so a group deleted
		// from under us degrades to its parent rather than dangling.
		flow::Graph& graph = resolvePath(appDelegate.graph(), m_ctx.activePath); // mutated by the canvas below
		// ... and the runtime state that belongs to it. An Evaluation is a tree with one child per
		// group node, so the SAME path walks it — every pane gets a definition and its values in step.
		flow::Evaluation& evaluation = resolveEvaluation(appDelegate.evaluation(), m_ctx.activePath);
		// The path the panes are about to draw with. Captured now because a pane may NAVIGATE during
		// this frame (a double-click descends), and the positions collected at the end of the frame
		// belong to the level that was actually on screen — not to the one we are moving to.
		const GraphPath drawnPath = m_ctx.activePath;

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
		m_canvas.draw(m_ctx, graph, evaluation, edited);

		// The application menu bar (viewport-top; ImGui places it there regardless of call order). Its
		// Add feeds the same `edited` flag as the canvas, so a menu Add Node re-runs the scene like any edit.
		m_menuBar.draw(m_ctx, graph, window.app(), edited, m_resetLayout);

		// A topology edit re-runs the scene, so the previews need refreshing (recomputed
		// images, added/removed ports). Mark them dirty; refreshIfDirty below upserts in
		// place where it can. (Deferred, not per-frame — acm::Texture::upload is a stalling
		// synchronous submit.)
		if (edited)
		{
			appDelegate.reevaluate(); // always runs the ROOT: the plan expands groups, so one run covers every level
			m_previews.markDirty();
			m_ctx.markChanged(); // a topology edit -> unsaved changes + an undo snapshot
			m_ctx.loadIssues.clear();
		}

		m_previews.refreshIfDirty(graph, evaluation, *m_guiCtx);

		// The per-node Inspector — reads (and param-edits) the now post-edit graph.
		m_inspector.draw(m_ctx, graph, evaluation, m_previews, m_paramEditors);

		// The graph's I/O boundary — the host-binding surface (bind inputs, save outputs).
		m_interface.draw(m_ctx, graph, evaluation, m_previews, m_paramEditors);

		// Preview + Issues panels — docked windows the default layout tiles alongside the Graph.
		m_preview.draw(m_ctx, graph, evaluation, m_previews);
		m_issues.draw(m_ctx, graph, evaluation);

		// The unsaved-changes guard for New (opened by requestNew when m_ctx.dirty).
		m_menuBar.drawConfirmModal(m_ctx, graph);

		// A group's ports mirror its interior, so re-derive them for every group we are inside — the
		// Interface panel's ± and renames act on the INNER boundary, and nothing else would carry that
		// out to the group's own face (or to the parent's edges into it). Done after the panes drew, so
		// this frame's edits are included; idempotent, so a quiet frame costs a pin walk.
		if (!drawnPath.empty() && syncPathGroups(appDelegate.graph(), drawnPath))
		{
			appDelegate.reevaluate();
			m_previews.markDirty();
		}

		// Capture THIS level's positions into the layout tree, now that the canvas has drawn them.
		// imnodes only knows about the level on screen, so every other level keeps the positions
		// captured when it was last shown — which is why the tree is kept here rather than read from
		// imnodes on demand.
		layoutAt(m_ctx.layout, drawnPath).nodes = collectLayout(m_ctx.canvas, graph);

		// A snapshot of the current document (structure + params + names + layout), computed only when
		// needed. The graph is always the ROOT — a snapshot is the whole document, not the level in view.
		const auto snapshotNow = [&]()
		{ return snapshotGraph(appDelegate.graph(), appDelegate.nodeFactory(), m_ctx.layout); };

		// Record an undo step for a committed edit — but only once no widget is active, so a param
		// drag (which markChanged()s every frame) coalesces into a single history entry on release.
		// push() ignores a snapshot equal to the current one, so a bound-value-only edit (not in the
		// document) records nothing. Done BEFORE the swap below, so it captures the pre-swap graph.
		if (m_ctx.pendingSnapshot && !gui::IsAnyItemActive())
		{
			m_ctx.undo.push(snapshotNow());
			m_ctx.pendingSnapshot = false;
		}

		// Apply a pending Load / Undo-Redo restore now — every panel has drawn with the current graph,
		// so swapping it here (not at the button) can't dangle the `graph` reference used above. Next
		// frame re-seeds positions from the loaded layout.
		if (m_ctx.loadRequested)
		{
			appDelegate.replaceGraph(std::move(m_ctx.loadedGraph));
			m_ctx.loadRequested = false;
			m_previews.clear(); // the old graph's cached thumbnails are gone
			m_previews.markDirty();
			m_canvas.onGraphReplaced(); // re-seed positions next frame + drop stale canvas selection
			m_ctx.layout = std::move(m_ctx.pendingLayout);
			m_ctx.pendingLayout = {};

			// Where to point the panes afterwards. An UNDO should leave the user where they were,
			// looking at what they just undid, not eject them to the root — and since a restore
			// preserves node ids, the path it captured still names the same groups. resolvePath is
			// tolerant, so an undo that really did remove the group lands on its parent. New/Open
			// carry no path, because there the document itself changed and the root is the honest
			// place to land.
			// Assigned directly rather than through navigateTo: onGraphReplaced has already asked for
			// the re-seed and cleared the selection, and raising pathChanged would clear the selection
			// AGAIN next frame — wiping the reselect applied just below.
			m_ctx.activePath = m_ctx.pendingPath.value_or(GraphPath{});
			m_ctx.pendingPath.reset();

			// A New/Open carries a fresh baseline (the swap resets the history — undo doesn't cross it);
			// an Undo/Redo restore carries none, so the existing history (its cursor already moved) stands.
			// The baseline's presence is also exactly "document identity changed", which is the one
			// thing that retires the canvas id mapping: an edit, a navigation and an identity-preserving
			// restore must all KEEP it, or imnodes' per-object state would scatter.
			if (m_ctx.pendingBaseline)
			{
				m_ctx.canvas.reset();
				m_ctx.undo.reset(std::move(*m_ctx.pendingBaseline));
				m_ctx.pendingBaseline.reset();
			}
			else
			{
				// An Undo/Redo restore: re-select the nodes that were selected before it, so a
				// param-drag undo doesn't drop the selection — and the selection-driven Inspector keeps
				// showing the node. onGraphReplaced cleared the selection just above; this puts it back.
				// The ids came through the restore unchanged, so a node that survived the edit is
				// re-selected and one that did not is simply absent. resolvePath also truncates a
				// path whose group the undo removed, so the panes land on its parent.
				flow::Graph& active = resolvePath(appDelegate.graph(), m_ctx.activePath);
				for (const flow::NodeId id : m_ctx.pendingReselect)
				{
					if (active.contains(id))
						gui::nodes::SelectNode(m_ctx.canvas.node(id));
				}
			}
			m_ctx.pendingReselect.clear();
			m_ctx.pendingSnapshot = false; // the swap itself is never an undoable edit
		}

		// Baseline the startup graph the first time round (nothing New/Open'd to seed the history).
		// Positions were seeded during this frame's canvas draw, so the layout is already live.
		if (!m_ctx.undo.hasBaseline())
			m_ctx.undo.reset(snapshotNow());

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
