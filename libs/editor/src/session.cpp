#include "lain/editor/session.h"

#include <lain/core/paths.h> // configDir (~/.<app>)
#include <lain/core/uri.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/log/log.h>

#include <system_error>

namespace lain::editor
{
	std::filesystem::path sessionFile(std::string_view app)
	{
		const std::filesystem::path dir = core::configDir(app);
		if (dir.empty())
			return {};
		return dir / "session.json";
	}

	std::optional<data::Value> loadSessionDocument(const std::filesystem::path& file)
	{
		// Checked, not attempted: no session file is the normal first-run state, and io::data::load
		// logs a failed read — an error line on every fresh install would be noise, not a diagnostic.
		std::error_code ec;
		if (file.empty() || !std::filesystem::exists(file, ec))
			return std::nullopt;
		// Unreadable / not valid json — io::data::load logged it; convenience state, so carry on.
		return io::data::load(core::Uri::fromPath(file));
	}

	void saveSessionDocument(const std::filesystem::path& file, const data::Value& document)
	{
		if (file.empty())
			return;
		if (!io::data::save(core::Uri::fromPath(file), document))
			log::warn("session: could not write {}", file.string()); // convenience state only — nothing to interrupt
	}

	void detail::warnSessionShape(const std::filesystem::path& file)
	{
		log::warn("session: {} doesn't match the expected shape — starting from a clean session", file.string());
	}
} // namespace lain::editor
