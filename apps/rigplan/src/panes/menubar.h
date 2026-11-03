#pragma once

#include "../appcontext.h"
#include "specifications.h"

#include <filesystem>

namespace lain::app
{
	class Application;
}

namespace rigplan
{
	// The application menu bar and its shortcuts (Cmd on macOS, Ctrl elsewhere, as flowview's):
	//   File: New, Open..., Open Recent, Save, Save As..., Quit;
	//   Edit: Undo, Redo, Key at Current Frame (K), Duplicate, Delete;
	//   Add: Pole, Fixed Point, Camera, Light, Plane, Box, Prop..., Board, Room (a camera or light on the
	//        selected mount);
	//   Specifications: Copy In from Catalogue..., Save to Catalogue..., Save to New Catalogue...
	//        (specifications.h);
	//   View: Frame All (Home), Frame Selected (F), Preview Light, Frustum Depth, Reset Layout.
	// New, Open, Open Recent and Quit replace or end the open plan, so each goes through the document's
	// guard, as does the window's close button (MainWindow::onCloseRequested): with unsaved work it asks
	// first, and a Save that is cancelled or fails cancels the request with it. Stateless: what it reads
	// and writes is the AppContext.
	struct MenuBarPane
	{
		void draw(AppContext& ctx, lain::app::Application& app, bool& resetLayout);

		// The menu bar's questions: the unsaved-changes one, opened when the guard holds a request, which
		// carries the request out once answered, and the Specifications menu's picker and clash question.
		// Call LATE in the frame, so the modals sit on a clean id stack. Escape is Cancel in each.
		void drawModals(AppContext& ctx, lain::app::Application& app);

		// Ask for `request` through the guard. Returns whether it was carried out now: with nothing
		// unsaved it is, and otherwise it waits for the modal.
		bool request(AppContext& ctx, lain::app::Application& app, DocumentRequest request);

		// Open the plan at `path` as the document, its paths read against its own folder. A plan that
		// does not read keeps the open one, with the reasons in the Issues pane, and leaves Open Recent
		// if the file is gone. Returns whether it opened. Public, as the window reopens a plan at launch.
		bool openPath(AppContext& ctx, const std::filesystem::path& path);

		// Save to the document's own file, or ask for one when it has none (saveAs). Whether it saved: a
		// cancelled panel or a failed write did not.
		bool save(AppContext& ctx);
		bool saveAs(AppContext& ctx);

	private:
		void perform(AppContext& ctx, lain::app::Application& app, const DocumentRequest& request);
		void drawOpenRecent(AppContext& ctx, lain::app::Application& app);

		SpecificationsMenu m_specifications; // stateless too: what it waits on is the AppContext's
	};
} // namespace rigplan
