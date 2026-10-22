#include "lain/editor/recents.h"

#include <algorithm>

namespace lain::editor
{
	void noteDocument(RecentDocuments& documents, const std::filesystem::path& path, std::size_t capacity)
	{
		if (path.empty())
			return;

		documents.last = path;

		// Most recent first: drop any existing entry for this file, push it to the front, trim the tail.
		auto& recent = documents.recent;
		recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
		recent.insert(recent.begin(), path);
		if (recent.size() > capacity)
			recent.resize(capacity);
	}

	void forgetDocument(RecentDocuments& documents, const std::filesystem::path& path)
	{
		auto& recent = documents.recent;
		recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
		if (documents.last == path)
			documents.last.clear();
	}
} // namespace lain::editor
