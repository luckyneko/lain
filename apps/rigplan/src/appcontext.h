#pragma once

#include "document.h"
#include "selection.h"
#include "session.h"

#include <lain/editor/guard.h>

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
