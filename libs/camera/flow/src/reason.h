#pragma once

#include <string>
#include <vector>

namespace lain::camera::detail
{
	// A result's diagnostics (board::PatternResult, SpecificationResult, RenderResult) as ONE reason,
	// for NodeEvaluation::fail: a node fails once, however many problems it was told about, so the
	// reason names all of them.
	template <typename Diagnostic>
	std::string reason(const std::vector<Diagnostic>& diagnostics)
	{
		std::string text;
		for (const Diagnostic& diagnostic : diagnostics)
		{
			if (!text.empty())
				text += "; ";
			text += diagnostic.detail;
		}
		return text;
	}
} // namespace lain::camera::detail
