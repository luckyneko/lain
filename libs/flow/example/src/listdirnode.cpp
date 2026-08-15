#include "lain/flow/example/listdirnode.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace lain::flow::example
{
	ListDirNode::ListDirNode(std::string directory)
		: Node("ListDir")
	{
		// Both settings are inputs WITH DEFAULTS: configured on the node when nothing is wired, driven
		// by the graph when something is. A path default still renders as a picker (ADR-0005 — the
		// type carries the widget intent); what changed is that it is also a pin.
		m_dir = addInput<std::filesystem::path>("directory", Default{std::filesystem::path(std::move(directory))});
		m_filter = addInput<std::string>("extension", Default{std::string{}});
		m_out = addOutput<std::vector<std::filesystem::path>>("files");
	}

	void ListDirNode::compute(NodeEvaluation& evaluation) const
	{
		// Read as ordinary inputs: unconnected, the slot carries the declared default, so there is
		// nothing to reconcile here at all.
		const std::filesystem::path dir = evaluation.input(m_dir).get<std::filesystem::path>();
		const std::string filter = evaluation.input(m_filter).get<std::string>();

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
