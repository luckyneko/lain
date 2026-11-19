#include "lain/io/framepattern.h"

#include <lain/core/parse.h>
#include <lain/string/pattern.h>

#include <algorithm>
#include <optional>
#include <system_error>
#include <utility>

namespace lain::io
{
	namespace fs = std::filesystem;

	// One matched file: its path, and the number that orders it.
	struct Numbered
	{
		std::string path;
		unsigned long long number = 0;
	};

	std::vector<std::string> numberedFiles(const fs::path& pattern)
	{
		// Parsed ONCE, outside the scan: a Pattern is asked about every entry in the directory.
		const lain::string::Pattern name{pattern.filename().string()};
		const std::string key{frameKey};

		const fs::path directory = pattern.has_parent_path() ? pattern.parent_path() : fs::path{"."};

		std::vector<Numbered> matched;
		std::error_code error;
		for (const fs::directory_entry& entry : fs::directory_iterator{directory, error})
		{
			if (!entry.is_regular_file())
				continue;

			const std::optional<lain::string::Captures> captures = name.match(entry.path().filename().string());
			if (!captures)
				continue;

			// What a capture MEANS is asked here rather than by the matcher, which delimits by the
			// literals around a key and nothing else. This is where "shot.x.png" is rejected — and
			// it has to be asked anyway, since the number is what orders the sequence.
			const std::string& digits = captures->at(key);
			const std::optional<unsigned long long> number = lain::core::parse<unsigned long long>(digits);
			if (!number.has_value())
				continue;

			matched.push_back({entry.path().string(), *number});
		}

		std::sort(matched.begin(), matched.end(),
				  [](const Numbered& a, const Numbered& b)
				  { return a.number < b.number; });

		std::vector<std::string> paths;
		paths.reserve(matched.size());
		for (Numbered& entry : matched)
			paths.push_back(std::move(entry.path));
		return paths;
	}
} // namespace lain::io
