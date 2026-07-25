#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace flowview
{
	// What flowview remembers *between* runs — the small conveniences a document app is expected to
	// have: reopen what you had open, list what you had open before that, and put the file dialogs
	// back where you left them. Stored as JSON in ~/.flowview/session.json (core::configDir),
	// alongside the dock layout's imgui.ini — one findable, clearable place for the app's state.
	//
	// This is user convenience, NOT document data: a lost or malformed session file costs nothing
	// (every field falls back to its default and the app opens blank), so load never errors and the
	// file is written best-effort. Deliberately not a "settings" store — when real preferences turn
	// up they can join this struct or take their own file; nothing here presumes either.
	struct Session
	{
		// The graph the last run was working on, reopened at the next launch (unless --example). Empty
		// for an untitled document — New clears it, since there is nothing to reopen.
		std::filesystem::path lastGraph;

		// Graphs opened / saved, most recent first, deduplicated, capped at maxRecentGraphs — the
		// File ▸ Open Recent list. Entries are kept as written; a path that has since vanished is
		// pruned when opening it fails, not eagerly on load (the file may live on a volume that
		// isn't mounted right now).
		std::vector<std::filesystem::path> recentGraphs;

		// The directory the file dialogs should start in — lain::gui's dialog seam owns this within a
		// session (gui::lastDirectory()); persisting it is what makes it survive a restart.
		std::filesystem::path lastDialogDir;
	};

	// How many entries File ▸ Open Recent keeps.
	inline constexpr std::size_t maxRecentGraphs = 10;

	// Read ~/.flowview/session.json. A missing, unreadable, or malformed file is not an error — it
	// yields a default Session (first run looks exactly like a cleared one).
	Session loadSession();

	// Write ~/.flowview/session.json. Best-effort: a failure is logged, never surfaced to the user —
	// losing convenience state must not interrupt what they were doing.
	void saveSession(const Session& session);

	// Record that `path` is now the current document: it becomes lastGraph and moves to the front of
	// recentGraphs (deduplicated, capped). Call for both Open and Save — either makes it the file the
	// user is working on.
	void noteGraphPath(Session& session, const std::filesystem::path& path);

	// Drop `path` from the recents (and from lastGraph if it is that) — for a file that failed to
	// open, so a stale entry doesn't linger in the menu.
	void forgetGraphPath(Session& session, const std::filesystem::path& path);
} // namespace flowview
