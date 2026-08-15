#include "lain/gui/dialogs.h"

#include <portable-file-dialogs.h>

#include <utility>

namespace lain::gui
{
	// pfd takes filters as a flat vector alternating {label, space-joined-patterns, ...}. An empty
	// list becomes an all-files filter so the picker isn't left with none.
	static std::vector<std::string> toPfdFilters(const std::vector<FileFilter>& filters)
	{
		if (filters.empty())
			return {"All files", "*"};

		std::vector<std::string> out;
		out.reserve(filters.size() * 2);
		for (const FileFilter& filter : filters)
		{
			std::string patterns;
			for (const std::string& pattern : filter.patterns)
			{
				if (!patterns.empty())
					patterns += ' ';
				patterns += pattern;
			}
			out.push_back(filter.name);
			out.push_back(std::move(patterns));
		}
		return out;
	}

	// Session memory of the directory the last dialog resolved to — shared by open + save (the
	// single "recent folder" users expect), so a dialog with no explicit defaultDir resumes where
	// the user last was. Persisting it across runs is the app's call, through the accessors below.
	static std::filesystem::path& lastDir()
	{
		static std::filesystem::path dir;
		return dir;
	}

	std::optional<std::filesystem::path> openFile(const std::string& title,
												  const std::filesystem::path& defaultDir,
												  const std::vector<FileFilter>& filters)
	{
		const std::filesystem::path start = defaultDir.empty() ? lastDir() : defaultDir;
		const std::vector<std::string> result = pfd::open_file(title, start.string(), toPfdFilters(filters)).result();
		if (result.empty())
			return std::nullopt;
		const std::filesystem::path picked(result.front());
		lastDir() = picked.parent_path();
		return picked;
	}

	std::optional<std::filesystem::path> selectFolder(const std::string& title, const std::filesystem::path& defaultDir)
	{
		const std::filesystem::path start = defaultDir.empty() ? lastDir() : defaultDir;
		const std::string result = pfd::select_folder(title, start.string()).result();
		if (result.empty())
			return std::nullopt;
		const std::filesystem::path picked(result);
		// The folder ITSELF is where the next dialog should open, not its parent — unlike a picked
		// file, whose directory is the interesting part.
		lastDir() = picked;
		return picked;
	}

	std::optional<std::filesystem::path> saveFile(const std::string& title,
												  const std::filesystem::path& defaultDir,
												  const std::vector<FileFilter>& filters)
	{
		const std::filesystem::path start = defaultDir.empty() ? lastDir() : defaultDir;
		const std::string result = pfd::save_file(title, start.string(), toPfdFilters(filters)).result();
		if (result.empty())
			return std::nullopt;
		const std::filesystem::path picked(result);
		lastDir() = picked.parent_path();
		return picked;
	}

	std::filesystem::path lastDirectory()
	{
		return lastDir();
	}

	void setLastDirectory(std::filesystem::path dir)
	{
		lastDir() = std::move(dir);
	}

	void message(const std::string& title, const std::string& text, bool error)
	{
		pfd::message(title, text, pfd::choice::ok, error ? pfd::icon::error : pfd::icon::info).result();
	}
} // namespace lain::gui
