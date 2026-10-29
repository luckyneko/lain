#include "session.h"

#include <lain/data/archive.h>
#include <lain/editor/session.h> // sessionFile / loadSession / saveSession

namespace rigplan
{
	using namespace lain;

	// The file's shape IS this struct, with the recents written flat. Absent keys load as defaults, so a
	// file written before a field existed still reads.
	void serialize(data::Archive& ar, Session& session)
	{
		ar.member("lastPlan", session.plans.last).member("recentPlans", session.plans.recent).member("lastDialogDir", session.lastDialogDir);
	}

	std::filesystem::path sessionFile()
	{
		return editor::sessionFile("rigplan");
	}

	Session loadSession(const std::filesystem::path& file)
	{
		return editor::loadSession<Session>(file);
	}

	void saveSession(const std::filesystem::path& file, const Session& session)
	{
		editor::saveSession(file, session);
	}
} // namespace rigplan
