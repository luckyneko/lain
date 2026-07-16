#include "lain/core/paths.h"

#include "lain/core/platform.h" // envVar

#include <string>
#include <system_error>

namespace lain::core
{
	std::filesystem::path homeDir()
	{
		if (const std::string home = envVar("HOME"); !home.empty())
			return home;
		if (const std::string profile = envVar("USERPROFILE"); !profile.empty())
			return profile;
		return {};
	}

	std::filesystem::path configDir(std::string_view appName)
	{
		const std::filesystem::path home = homeDir();
		if (home.empty())
			return {};
		const std::filesystem::path dir = home / ("." + std::string(appName));
		std::error_code ec;
		std::filesystem::create_directories(dir, ec); // best-effort: fine if it already exists
		return dir;
	}
} // namespace lain::core
