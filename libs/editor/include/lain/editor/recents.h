#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace lain::editor
{
	// The documents an editor has had open: the one to reopen at the next launch, and the Open
	// Recent list. Plain data an app keeps in its session, so it decides the keys it persists them
	// under; noteDocument and forgetDocument are the one rule for how the two change.
	struct RecentDocuments
	{
		// The document to reopen at the next launch. Empty for an untitled document — New clears it,
		// since there is nothing to reopen.
		std::filesystem::path last;

		// Documents opened or saved, most recent first, deduplicated, capped. Entries are kept as
		// written; a path that has since vanished is pruned when opening it fails (forgetDocument),
		// not eagerly on load, since the file may live on a volume that isn't mounted right now.
		std::vector<std::filesystem::path> recent;
	};

	// How many entries an Open Recent list keeps.
	inline constexpr std::size_t maxRecentDocuments = 10;

	// Record that `path` is now the current document: it becomes `last` and moves to the front of
	// `recent` (deduplicated, capped at `capacity`). Call for both Open and Save — either makes it the
	// file the user is working on. An empty path changes nothing.
	void noteDocument(RecentDocuments& documents, const std::filesystem::path& path,
					  std::size_t capacity = maxRecentDocuments);

	// Drop `path` from `recent`, and from `last` if it is that — for a file that failed to open, so a
	// stale entry doesn't linger in the menu.
	void forgetDocument(RecentDocuments& documents, const std::filesystem::path& path);
} // namespace lain::editor
