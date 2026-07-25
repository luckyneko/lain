#include "session.h"

#include <lain/core/paths.h> // configDir (~/.flowview)
#include <lain/data/data.h>	 // toValue / fromValue
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/log/log.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace flowview
{
	using namespace lain;

	// The session file's shape IS this struct (data's "the type is the schema"). Absent keys load as
	// defaults, so an older file — or one written before a field existed — still reads.
	LAIN_SERIALIZE(Session, lastGraph, recentGraphs, lastDialogDir)

	// ~/.flowview/session.json — beside the dock layout's imgui.ini. Empty if the home directory
	// can't be resolved, in which case the session simply isn't persisted.
	static std::filesystem::path sessionFile()
	{
		const std::filesystem::path dir = core::configDir("flowview");
		if (dir.empty())
			return {};
		return dir / "session.json";
	}

	Session loadSession()
	{
		const std::filesystem::path file = sessionFile();
		// Checked, not attempted: no session file is the normal first-run state, and io::data::load
		// logs a failed read — an error line on every fresh install would be noise, not a diagnostic.
		std::error_code ec;
		if (file.empty() || !std::filesystem::exists(file, ec))
			return {};

		const auto document = io::data::load(file.string());
		if (!document)
			return {}; // unreadable / not valid json — io::data::load logged it; convenience state, so carry on

		std::optional<Session> session = data::fromValue<Session>(*document);
		if (!session)
		{
			log::warn("session: {} doesn't match the expected shape — starting from a clean session", file.string());
			return {};
		}
		return std::move(*session);
	}

	void saveSession(const Session& session)
	{
		const std::filesystem::path file = sessionFile();
		if (file.empty())
			return;
		if (!io::data::save(file.string(), data::toValue(session)))
			log::warn("session: could not write {}", file.string()); // convenience state only — nothing to interrupt
	}

	void noteGraphPath(Session& session, const std::filesystem::path& path)
	{
		if (path.empty())
			return;

		session.lastGraph = path;

		// Most recent first: drop any existing entry for this file, push it to the front, trim the tail.
		auto& recent = session.recentGraphs;
		recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
		recent.insert(recent.begin(), path);
		if (recent.size() > maxRecentGraphs)
			recent.resize(maxRecentGraphs);
	}

	void forgetGraphPath(Session& session, const std::filesystem::path& path)
	{
		auto& recent = session.recentGraphs;
		recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
		if (session.lastGraph == path)
			session.lastGraph.clear();
	}
} // namespace flowview
