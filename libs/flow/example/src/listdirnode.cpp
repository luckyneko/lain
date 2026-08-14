#include "lain/flow/example/listdirnode.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace lain::flow::example
{
	ListDirNode::ListDirNode(std::string directory)
		: Node("ListDir")
	{
		// A path param, so the adapter offers a picker rather than a text field (ADR-0005).
		m_dir = addParam<std::filesystem::path>("directory", std::filesystem::path(std::move(directory)));
		m_filter = addParam<std::string>("extension", std::string{});
		m_out = addOutput<std::vector<std::filesystem::path>>("files");
	}

	void ListDirNode::compute(NodeEvaluation& evaluation) const
	{
		const std::filesystem::path dir = param(m_dir).get<std::filesystem::path>();
		const std::string filter = param(m_filter).get<std::string>();

		// A missing or unreadable directory yields NO VALUE rather than an empty list, and the
		// difference matters: an empty list is a legitimate answer that maps to an empty result,
		// while "there is no such folder" should suppress downstream through ADR-0007's ordinary
		// gate. The error_code overloads keep a bad path from throwing out of compute().
		std::error_code ec;
		if (dir.empty() || !std::filesystem::is_directory(dir, ec) || ec)
		{
			evaluation.output(m_out).clear();
			return;
		}

		std::vector<std::filesystem::path> files;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (!entry.is_regular_file())
				continue;
			if (!filter.empty() && entry.path().extension() != filter)
				continue;
			files.push_back(entry.path());
		}
		if (ec)
		{
			evaluation.output(m_out).clear();
			return;
		}

		// Sorted, so element i names the same file on every run. A map identifies its children
		// positionally, so an unstable order would silently re-target every element.
		std::sort(files.begin(), files.end());
		evaluation.output(m_out).set(std::move(files));
	}
} // namespace lain::flow::example
