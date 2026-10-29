#pragma once

#include "appcontext.h"
#include "panes/inspector.h"
#include "panes/issues.h"
#include "panes/menubar.h"
#include "panes/outliner.h"
#include "panes/statusbar.h"

#include <lain/app/windowdelegate.h>
#include <lain/gui/context.h>

#include <memory>
#include <string>

namespace rigplan
{
	// rigplan's window: the menu bar, the Outliner on the left, the Inspector on the right, the Viewport
	// between them with Issues below it, and the status bar, in one docking layout kept in
	// ~/.rigplan/imgui.ini. It owns the gui Context and the AppContext the panes share. Its title is the
	// plan's file name, with a * while it has unsaved changes. ImGui is single-threaded: every call here
	// is on the main thread.
	class MainWindow : public lain::app::WindowDelegate
	{
	public:
		bool onInit(lain::app::Window& window) override;
		void onRender(lain::app::Window& window, const lain::app::TimeState& time) override;
		// The close button goes through the document's guard, as Quit does: with unsaved work the window
		// stays open while the question is asked.
		bool onCloseRequested(lain::app::Window& window) override;
		void onShutdown(lain::app::Window& window) override;

	private:
		std::unique_ptr<lain::gui::Context> m_gui;
		AppContext m_ctx;
		MenuBarPane m_menuBar;
		OutlinerPane m_outliner;
		InspectorPane m_inspector;
		IssuesPane m_issues;
		StatusBarPane m_statusBar;

		// Stamp the default docks on the next frame: on the first run (no saved layout), with
		// --reset-layout, or from View > Reset Layout.
		bool m_seedLayout = false;
		bool m_resetLayout = false;
		std::string m_title; // what the title bar says, so it is set only when it changes
	};
} // namespace rigplan
