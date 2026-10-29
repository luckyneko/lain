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

	// The default docks: the Outliner on the left, the Inspector on the right, and between them the
	// Viewport over Issues. Window names must match the panes' gui::Begin.
	static void buildDefaultLayout(gui::DockNode dock)
	{
		gui::dockReset(dock);
		const auto [outliner, rest] = gui::dockSplit(dock, gui::DockDir::Left, 0.2f);
		const auto [inspector, middle] = gui::dockSplit(rest, gui::DockDir::Right, 0.3f);
		const auto [issues, viewport] = gui::dockSplit(middle, gui::DockDir::Down, 0.22f);
		gui::dockWindow(outliner, "Outliner");
		gui::dockWindow(inspector, "Inspector");
		gui::dockWindow(viewport, "Viewport");
		gui::dockWindow(issues, "Issues");
		gui::dockFinish(dock);
	}

	void MainWindow::onRender(app::Window& window, const app::TimeState&)
	{
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
		// The Viewport's place in the layout, held until it is drawn in slice 8f.
		if (gui::Begin("Viewport"))
			gui::TextDisabled("The Viewport arrives with M15 slice 8f.");
		gui::End();
		m_issues.draw(m_ctx);
		m_statusBar.draw(m_ctx);
		m_menuBar.drawConfirmModal(m_ctx, window.app());

		// A selection whose object an edit or an undo removed is let go of, rather than left naming nothing.
		if (!exists(m_ctx.document.plan, m_ctx.selection))
			m_ctx.selection = Selection::nothing();

		// One step of history per gesture: a drag in the Inspector, or of the gizmo, is recorded once it
		// ends, however many frames it changed the plan for.
		if (m_ctx.document.pendingSnapshot && !gui::IsAnyItemActive() && !gui::gizmo::IsUsing())
			commit(m_ctx.document);

		const std::string name = m_ctx.document.path.empty() ? std::string("Untitled") : m_ctx.document.path.filename().string();
		const std::string title = string::format("{}{} - rigplan", name, m_ctx.document.dirty ? " *" : "");
		if (title != m_title)
		{
			window.setTitle(title);
			m_title = title;
		}

		window.renderer().render([&](acm::CommandBuffer cmd, std::uint32_t frame)
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
		m_gui.reset(); // before the device goes
	}
} // namespace rigplan
