#pragma once

#include <lain/data/data.h> // toValue / fromValue

#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

namespace lain::editor
{
	// What an editor remembers *between* runs — the small conveniences a document app is expected to
	// have: reopen what you had open, list what you had open before that, and put the file dialogs
	// back where you left them. Each app declares its own session struct (its shape IS the file, by
	// data's "the type is the schema") and persists it here, as JSON beside the app's other state.
	//
	// This is user convenience, NOT document data: a lost or malformed session file costs nothing
	// (every field falls back to its default and the app opens blank), so loading never errors and
	// the file is written best-effort. Deliberately not a "settings" store — when real preferences
	// turn up they can join an app's struct or take their own file; nothing here presumes either.
	//
	// The caller registers the data codecs first (io::data::registerDataCodecs), as for any load.

	// ~/.<app>/session.json (core::configDir), beside the app's dock layout. Empty if the home
	// directory can't be resolved, in which case the session simply isn't persisted.
	std::filesystem::path sessionFile(std::string_view app);

	// Read a session file as a document. A missing file is the normal first-run state and yields
	// nothing without a word; an unreadable or invalid one is logged by the read and yields nothing.
	std::optional<lain::data::Value> loadSessionDocument(const std::filesystem::path& file);

	// Write a session document. Best-effort: a failure is logged, never surfaced to the user — losing
	// convenience state must not interrupt what they were doing. An empty path writes nothing.
	void saveSessionDocument(const std::filesystem::path& file, const lain::data::Value& document);

	namespace detail
	{
		// Logs that `file` does not have the session's shape, and that the session starts clean.
		void warnSessionShape(const std::filesystem::path& file);
	} // namespace detail

	// Read `file` as a session of type `Session`. A missing, unreadable or malformed file is not an
	// error — it yields a default Session, so a first run looks exactly like a cleared one.
	template <typename Session>
	Session loadSession(const std::filesystem::path& file)
	{
		const std::optional<lain::data::Value> document = loadSessionDocument(file);
		if (!document)
			return {};
		std::optional<Session> session = lain::data::fromValue<Session>(*document);
		if (!session)
		{
			detail::warnSessionShape(file);
			return {};
		}
		return std::move(*session);
	}

	// Write `session` to `file`, best-effort (see saveSessionDocument).
	template <typename Session>
	void saveSession(const std::filesystem::path& file, const Session& session)
	{
		saveSessionDocument(file, lain::data::toValue(session));
	}
} // namespace lain::editor
