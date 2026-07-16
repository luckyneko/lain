#pragma once

#include <filesystem>
#include <string_view>

namespace lain::core
{
	// The user's home directory: $HOME (unix, and git-bash etc. on Windows), else %USERPROFILE%. Empty
	// if neither is set.
	std::filesystem::path homeDir();

	// The per-user config directory for an application: <home>/.<appName>, created on demand. One
	// findable, consistent location across macOS / Linux / Windows (home from $HOME, or %USERPROFILE%
	// on Windows) — the `~/.gitconfig` convention, easy to locate and to clear. Returns the directory
	// (existing or freshly created), or an empty path if the home directory can't be resolved.
	std::filesystem::path configDir(std::string_view appName);
} // namespace lain::core
