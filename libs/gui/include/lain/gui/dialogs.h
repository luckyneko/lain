#pragma once

// Native OS file dialogs — the "open/save file" panels a gui app needs to pick a path without a
// typed-out string. A service-shaped seam (free functions) fronting portable-file-dialogs, which
// is named nowhere past dialogs.cpp; the panels are the platform's own (osascript / zenity /
// Win32), so they look native on each OS. Blocking calls — invoke from the main thread.
//
// The seam remembers the last directory used within the session (shared by open + save), so a
// call with an empty defaultDir resumes where the user last was. It is not persisted across runs.

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
	// path, or std::nullopt on cancel. `defaultDir` is the starting *directory* — pass a directory,
	// not a file path (the macOS backend resolves it as a folder location).
	std::optional<std::filesystem::path> openFile(const std::string& title,
												  const std::filesystem::path& defaultDir = {},
												  const std::vector<FileFilter>& filters = {});

	// Native "save file" dialog. Blocks until the user picks a destination or cancels; returns the
	// chosen path, or std::nullopt on cancel. `defaultDir` is the starting *directory* — the macOS
	// backend (pfd 0.1.0) takes only a folder location here, not a prefilled filename.
	std::optional<std::filesystem::path> saveFile(const std::string& title,
												  const std::filesystem::path& defaultDir = {},
												  const std::vector<FileFilter>& filters = {});

	// Native message box with a single OK button (the third of the pfd trio, for surfacing an
	// error or notice the user must acknowledge). `error` picks the error icon over the info one.
	// Blocks until dismissed.
	void message(const std::string& title, const std::string& text, bool error = false);
} // namespace lain::gui
