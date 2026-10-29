#pragma once

#include "document.h"
#include "selection.h"
#include "session.h"

#include <lain/editor/guard.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rigplan
{
	// What would throw away the open plan, and so waits for the unsaved-changes guard: a New, an Open
	// (of `path`, or of whatever the Open... dialog picks when it is empty), or the app quitting.
	struct DocumentRequest
	{
		enum class Kind
		{
			New,
			Open,
			Quit,
		};
		Kind kind = Kind::New;
		std::filesystem::path path;
	};

	// What the View menu sets and the Viewport reads.
	struct ViewOptions
	{
		// An ambient light of 0.15 added to the plan's own, so a plan with no lights is not black. The
		// Viewport's alone: a rig plan states no ambient (renderScene), so a capture never sees it.
		bool previewLight = true;
		double frustumDepth = 1.0; // metres a camera's frustum and a spot's cone reach
		// Asked for by Frame All (Home) and Frame Selected (F), and done by the Viewport.
		bool frameAll = false;
		bool frameSelected = false;
	};

	// The model rigplan's panes share, owned by the main window: the open document, what is selected,
	// what is remembered between runs, the guard on the document, and what the app has to say. No ImGui
	// in it, and nothing a pane keeps from one frame to the next beyond this.
	struct AppContext
	{
		Document document = newDocument();
		Selection selection;
		Session session;
		// Where the session is kept: sessionFile() in the app, empty in a test, which writes nothing.
		std::filesystem::path sessionFile;
		lain::editor::DocumentGuard<DocumentRequest> guard;

		// Counts the plans opened or made, so the Viewport knows to frame a new one: the view is not kept
		// with a plan, so opening one frames all of it.
		std::uint64_t opened = 0;

		ViewOptions view;
		// The Viewport's asset jobs (a bake, a board drawn, a mesh read) still running, and why those that
		// failed made nothing, each naming its owner and file.
		std::size_t assetsPending = 0;
		std::vector<std::string> assetProblems;

		// Why the last plan opened was refused, until the next open: a plan that reads with problems keeps
		// the open document, and the Issues pane says why.
		std::vector<std::string> openProblems;

		// A message for a moment: an edit refused, a file that would not open. Shown in the Issues pane,
		// and in the status bar, for about four seconds.
		std::string message;
		bool messageIsError = false;
		int messageFrames = 0;

		void note(std::string text, bool error = false)
		{
			message = std::move(text);
			messageIsError = error;
			messageFrames = 240; // about 4 s at 60 fps
		}

		// Keep the session as it is now: the recents and the plan to reopen change as plans are opened
		// and saved, so they are written then, and a crash loses none of them.
		void rememberSession() const { saveSession(sessionFile, session); }

		// Raised by a pane that changed the plan, or by the outcome of an edit: the document has unsaved
		// work, and a snapshot is recorded once the gesture ends.
		void changed() { document.markChanged(); }
	};
} // namespace rigplan
