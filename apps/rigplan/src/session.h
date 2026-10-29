#pragma once

#include <lain/editor/recents.h> // RecentDocuments: the last plan and Open Recent

#include <filesystem>

namespace lain::data
{
	class Archive;
}

namespace rigplan
{
	// What rigplan remembers between runs (lain::editor's session): the plan to reopen, Open Recent, and
	// where the file dialogs were. JSON in ~/.rigplan/session.json, beside the dock layout's imgui.ini, as
	// flowview's is in ~/.flowview. Convenience state, so a lost or malformed file costs nothing.
	struct Session
	{
		// The plan to reopen at the next launch (unless one is named on the command line), and File ▸ Open
		// Recent, changed only through editor::noteDocument / forgetDocument.
		lain::editor::RecentDocuments plans;

		// The folder the file dialogs start in: lain::gui's dialog seam owns it within a run
		// (gui::lastDirectory()), and this is what carries it to the next.
		std::filesystem::path lastDialogDir;
	};

	// The session file's shape (lain::data's hook, found by ADL): {lastPlan, recentPlans, lastDialogDir}.
	void serialize(lain::data::Archive& ar, Session& session);

	// ~/.rigplan/session.json (editor::sessionFile), or empty when the home folder cannot be found.
	std::filesystem::path sessionFile();

	// Read `file`: a default Session when it is missing, unreadable or malformed.
	Session loadSession(const std::filesystem::path& file);

	// Write `file`, best-effort: a failure is logged, never shown. An empty path writes nothing, which is
	// how a test keeps its hands off the real home folder.
	void saveSession(const std::filesystem::path& file, const Session& session);
} // namespace rigplan
