#pragma once

#include "runpolicy.h" // RunStrategy — the scheduler is remembered per session

#include <lain/editor/recents.h> // RecentDocuments — the last graph and Open Recent

#include <filesystem>

namespace lain::data
{
	class Archive;
}

namespace flowview
{
	// What flowview remembers *between* runs (lain::editor's session: reopen what you had open, list
	// what you had open before that, put the file dialogs back where you left them). Stored as JSON in
	// ~/.flowview/session.json (editor::sessionFile), alongside the dock layout's imgui.ini — one
	// findable, clearable place for the app's state. Convenience state, so a lost or malformed file
	// costs nothing.
	struct Session
	{
		// The graph to reopen at the next launch (unless --example) and File ▸ Open Recent, changed
		// only through editor::noteDocument / forgetDocument. Written under the keys `lastGraph` and
		// `recentGraphs`, which the file has always used, so a session written before the hoist into
		// lain::editor still reads.
		lain::editor::RecentDocuments graphs;

		// The directory the file dialogs should start in — lain::gui's dialog seam owns this within a
		// session (gui::lastDirectory()); persisting it is what makes it survive a restart.
		std::filesystem::path lastDialogDir;

		// The scheduler runs go through (Run ▸ Scheduler). Per SESSION rather than per document: it is
		// a choice about this machine and this sitting, not about a graph — unlike the run trigger,
		// which travels with the file. The app owns the live choice; this is its persistence, the same
		// arrangement as lastDialogDir.
		RunStrategy scheduler = RunStrategy::Parallel;
	};

	// How a Session maps to and from its file (lain::data's hook, found by ADL). Declared here so a
	// test can hold the file's shape still; the recents are written flat, under their old keys.
	void serialize(lain::data::Archive& ar, Session& session);

	// Read ~/.flowview/session.json. A missing, unreadable, or malformed file is not an error — it
	// yields a default Session (first run looks exactly like a cleared one).
	Session loadSession();

	// Write ~/.flowview/session.json. Best-effort: a failure is logged, never surfaced to the user —
	// losing convenience state must not interrupt what they were doing.
	void saveSession(const Session& session);
} // namespace flowview
