#include "mainwindow.h"

#include "rigplanapp.h"

#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/core/paths.h>  // core::configDir: ~/.rigplan, for the layout
#include <lain/gui/dialogs.h> // lastDirectory / setLastDirectory: the dialog folder, kept between runs
#include <lain/gui/dock.h>
#include <lain/gui/gizmo.h> // IsUsing: a gizmo drag, like a widget's, is one step of history
#include <lain/gui/gui.h>
#include <lain/string/format.h>

#include <archimedes/archimedes.h>

#include <filesystem>
#include <string>

namespace rigplan
{
	using namespace lain;

	bool MainWindow::onInit(app::Window& window)
	{
		const RigplanApp& delegate = window.app().getDelegate<RigplanApp>();
		const std::filesystem::path iniPath = core::configDir("rigplan") / "imgui.ini";
		m_seedLayout = delegate.resetLayout() || !std::filesystem::exists(iniPath);
		gui::ContextConfig config;
		config.iniFilename = iniPath.string();
		config.docking = true;
		m_gui = std::make_unique<gui::Context>(window.app(), window, config);
		m_viewport = std::make_unique<ViewportPane>(window.app().device(), *m_gui);
		m_cameraView = std::make_unique<CameraViewPane>(window.app().device(), *m_gui);
		m_visibility = std::make_unique<VisibilityPane>(window.app().device(), *m_gui);

		// What carried over: the dialogs' folder, and the plan to open, the one named on the command line
		// or else the one open last. One that does not open leaves a new plan, and the Issues pane says why.
		m_ctx.sessionFile = sessionFile();
		m_ctx.session = loadSession(m_ctx.sessionFile);
		gui::setLastDirectory(m_ctx.session.lastDialogDir);
		const std::filesystem::path open = !delegate.planToOpen().empty() ? std::filesystem::path(delegate.planToOpen()) : m_ctx.session.plans.last;
		if (!open.empty())
			m_menuBar.openPath(m_ctx, open);
		return true;
	}

	// The default docks: the Outliner on the left, the Inspector over the camera view and the visibility
	// map (two tabs, the camera view's in front) on the right, and between them the Viewport over the
	// Timeline over Issues. Window names must match the panes' gui::Begin.
	static void buildDefaultLayout(gui::DockNode dock)
	{
		gui::dockReset(dock);
		const auto [outliner, rest] = gui::dockSplit(dock, gui::DockDir::Left, 0.2f);
		const auto [right, middle] = gui::dockSplit(rest, gui::DockDir::Right, 0.3f);
		const auto [cameraView, inspector] = gui::dockSplit(right, gui::DockDir::Down, 0.4f);
		const auto [issues, upper] = gui::dockSplit(middle, gui::DockDir::Down, 0.18f);
		const auto [timeline, viewport] = gui::dockSplit(upper, gui::DockDir::Down, 0.22f);
		gui::dockWindow(outliner, "Outliner");
		gui::dockWindow(inspector, "Inspector");
		gui::dockWindow(cameraView, "Visibility");
		gui::dockWindow(cameraView, "Camera View");
		gui::dockWindow(viewport, "Viewport");
		gui::dockWindow(timeline, "Timeline");
		gui::dockWindow(issues, "Issues");
		gui::dockFinish(dock);
	}

	void MainWindow::onRender(app::Window& window, const app::TimeState&)
	{
		// What the asset jobs made since the last frame, FIRST, so this frame builds with it. Never waits.
		m_ctx.assets.poll();
		m_gui->newFrame();
		const gui::DockNode dock = gui::dockSpaceOverViewport();
		if (m_seedLayout || m_resetLayout)
		{
			buildDefaultLayout(dock);
			m_seedLayout = false;
			m_resetLayout = false;
		}

		m_menuBar.draw(m_ctx, window.app(), m_resetLayout);
		m_outliner.draw(m_ctx);
		m_inspector.draw(m_ctx);
		m_viewport->draw(m_ctx);
		m_cameraView->draw(m_ctx);
		m_visibility->draw(m_ctx);
		m_timeline.draw(m_ctx);
		m_issues.draw(m_ctx);
		m_statusBar.draw(m_ctx);
		m_menuBar.drawConfirmModal(m_ctx, window.app());

		// A selection whose object an edit or an undo removed is let go of, rather than left naming nothing,
		// and the frame shown follows what is left: a selected key's, within the timeline otherwise.
		if (!exists(m_ctx.document.plan, m_ctx.selection))
			m_ctx.selection = Selection::nothing();
		m_ctx.followFrame();

		// One step of history per gesture: a drag in the Inspector, or of the gizmo, is recorded once it
		// ends, however many frames it changed the plan for. The same boundary holds back the plan's asset
		// jobs, so a drag bakes only where it ends.
		const bool gesture = gui::IsAnyItemActive() || gui::gizmo::IsUsing();
		if (m_ctx.document.pendingSnapshot && !gesture)
			commit(m_ctx.document);

		// The plan's assets, now every pane has had its say: the plan's snapshot taken and compared once for
		// every reader, and what it lacks posted. Then the Viewport's scene and overlays for this frame, built
		// again when the plan, its assets or the frame shown changed, and recorded in the pre-pass below.
		m_ctx.assets.update(m_ctx.document.plan, gesture);
		m_viewport->update(m_ctx);
		m_cameraView->update(m_ctx, gesture);
		m_visibility->update(m_ctx, gesture);

		const std::string name = m_ctx.document.path.empty() ? std::string("Untitled") : m_ctx.document.path.filename().string();
		const std::string title = string::format("{}{} - rigplan", name, m_ctx.document.dirty ? " *" : "");
		if (title != m_title)
		{
			window.setTitle(title);
			m_title = title;
		}

		// The Viewport is drawn into its texture in the pre-pass, outside any rendering scope, and the pane
		// shows that texture when ImGui records the frame after it (gui::FrameTexture).
		window.renderer().render([&](acm::CommandBuffer& cmd, std::uint32_t frame)
								 { m_viewport->record(cmd, frame); },
								 [&](acm::CommandBuffer& cmd, std::uint32_t frame)
								 { m_gui->render(cmd, frame); });
	}

	bool MainWindow::onCloseRequested(app::Window& window)
	{
		// With nothing unsaved this quits at once; otherwise the guard holds it for the modal, which
		// quits on Save or Discard and keeps the window on Cancel.
		return m_menuBar.request(m_ctx, window.app(), {DocumentRequest::Kind::Quit, {}});
	}

	void MainWindow::onShutdown(app::Window&)
	{
		// The dialog folder as it ended up; the recents were written as they changed.
		m_ctx.session.lastDialogDir = gui::lastDirectory();
		m_ctx.rememberSession();
		// The plan's asset jobs are waited for first: quitting while a bake runs costs that bake, never a
		// crash. Then the visibility map's, the camera view's and the Viewport's GPU state (the map waits for
		// its counts and the camera view for its ray table first), and the backend, before the device goes.
		m_ctx.assets.wait();
		m_visibility->release();
		m_visibility.reset();
		m_cameraView->release();
		m_cameraView.reset();
		m_viewport->release();
		m_viewport.reset();
		m_gui.reset();
	}
} // namespace rigplan
