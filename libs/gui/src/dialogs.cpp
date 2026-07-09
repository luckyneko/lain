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

	std::optional<std::filesystem::path> openFile(const std::string& title,
												  const std::filesystem::path& defaultPath,
												  const std::vector<FileFilter>& filters)
	{
		const std::vector<std::string> result = pfd::open_file(title, defaultPath.string(), toPfdFilters(filters)).result();
		if (result.empty())
			return std::nullopt;
		return std::filesystem::path(result.front());
	}

	std::optional<std::filesystem::path> saveFile(const std::string& title,
												  const std::filesystem::path& defaultPath,
												  const std::vector<FileFilter>& filters)
	{
		const std::string result = pfd::save_file(title, defaultPath.string(), toPfdFilters(filters)).result();
		if (result.empty())
			return std::nullopt;
		return std::filesystem::path(result);
	}
} // namespace lain::gui
