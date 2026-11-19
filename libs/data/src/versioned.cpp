#include "lain/data/versioned.h"

#include <optional>
#include <utility>

namespace lain::data
{
	Value versionedDocument(std::uint32_t version, Value body)
	{
		Value document = Value::object();
		document.set("version", Value(std::uint64_t{version}));
		for (auto& [key, value] : *body.asObject())
			document.set(key, std::move(value));
		return document;
	}

	std::vector<std::string> versionProblems(const Value& document, const char* kind, std::uint32_t newest)
	{
		if (!document.isObject())
			return {std::string("a ") + kind + " document is an object"};
		const Value* version = document.find("version");
		const std::optional<std::uint64_t> number = version ? version->asUInt64() : std::nullopt;
		if (!number)
			return {"the document has no version"};
		if (*number > newest)
		{
			return {"the document is version " + std::to_string(*number) + ", newer than this build reads (" +
					std::to_string(newest) + ")"};
		}
		return {};
	}
} // namespace lain::data
