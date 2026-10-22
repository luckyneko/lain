#include "session.h"

#include <lain/data/archive.h>
#include <lain/editor/session.h> // sessionFile / loadSession / saveSession

namespace flowview
{
	using namespace lain;

	// The session file's shape IS this struct (data's "the type is the schema"), with the recents
	// written flat under the keys they had before lain::editor held them. Absent keys load as
	// defaults, so an older file — or one written before a field existed — still reads.
	void serialize(data::Archive& ar, Session& session)
	{
		ar.member("lastGraph", session.graphs.last)
			.member("recentGraphs", session.graphs.recent)
			.member("lastDialogDir", session.lastDialogDir)
			.member("scheduler", session.scheduler);
	}

	Session loadSession()
	{
		return editor::loadSession<Session>(editor::sessionFile("flowview"));
	}

	void saveSession(const Session& session)
	{
		editor::saveSession(editor::sessionFile("flowview"), session);
	}
} // namespace flowview
