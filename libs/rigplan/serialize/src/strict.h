#pragma once

#include <lain/data/data.h>
#include <lain/data/strict.h>
#include <lain/data/versioned.h>
#include <lain/string/format.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::rigplan::detail
{
	// The read every document here shares: the version, then the parts read tolerantly, then written
	// back and compared with what was stored, so anything the tolerant read passed over is a problem.
	// Not data::fromValueStrict: the version is a document field outside the parts, so the comparison
	// is against the whole document written back. `kind` names the document in a problem ("rig-plan").
	template <typename Doc>
	std::optional<Doc> readStrictly(const data::Value& document, const char* kind, std::uint32_t newest,
									std::vector<std::string>& problems)
	{
		problems = data::versionProblems(document, kind, newest);
		if (!problems.empty())
			return std::nullopt;
		std::optional<Doc> parts = data::fromValue<Doc>(document);
		if (!parts)
		{
			problems.push_back(string::format("the document does not have a {} document's shape", kind));
			return std::nullopt;
		}
		problems = data::shapeDifferences(document, data::versionedDocument(newest, data::toValue(*parts)));
		if (!problems.empty())
			return std::nullopt;
		return parts;
	}
} // namespace lain::rigplan::detail
