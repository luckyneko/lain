#pragma once

// Native OS file dialogs — the "open/save file" panels a gui app needs to pick a path without a
// typed-out string. A service-shaped seam (free functions) fronting portable-file-dialogs, which
// is named nowhere past dialogs.cpp; the panels are the platform's own (osascript / zenity /
// Win32), so they look native on each OS. Blocking calls — invoke from the main thread.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lain::gui
{
	// A file-type filter for a dialog: a human label plus its glob patterns —
	// {"Images", {"*.png", "*.jpg", "*.tiff"}}. An empty filter list shows all files.
	struct FileFilter
	{
		std::string name;
		std::vector<std::string> patterns;
	};

	// Native "open file" dialog. Blocks until the user picks a file or cancels; returns the chosen
	// path, or std::nullopt on cancel. `defaultPath` seeds the starting location.
	std::optional<std::filesystem::path> openFile(const std::string& title,
												  const std::filesystem::path& defaultPath = {},
												  const std::vector<FileFilter>& filters = {});

	// Native "save file" dialog. Blocks until the user picks a destination or cancels; returns the
	// chosen path, or std::nullopt on cancel. `defaultPath` seeds the starting location + filename.
	std::optional<std::filesystem::path> saveFile(const std::string& title,
												  const std::filesystem::path& defaultPath = {},
												  const std::vector<FileFilter>& filters = {});
} // namespace lain::gui
