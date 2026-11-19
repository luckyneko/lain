#include "versioned.h"

#include <lain/string/format.h>

#include <optional>
#include <utility>

namespace lain::camera
{
	data::Value versionedDocument(std::uint32_t version, data::Value body)
	{
		data::Value document = data::Value::object();
		document.set("version", data::Value(std::uint64_t{version}));
		for (auto& [key, value] : *body.asObject())
			document.set(key, std::move(value));
		return document;
	}

	std::vector<std::string> versionProblems(const data::Value& document, const char* kind, std::uint32_t newest)
	{
		if (!document.isObject())
			return {lain::string::format("a {} document is an object", kind)};
		const data::Value* version = document.find("version");
		const std::optional<std::uint64_t> number = version ? version->asUInt64() : std::nullopt;
		if (!number)
			return {"the document has no version"};
		if (*number > newest)
			return {lain::string::format("the document is version {}, newer than this build reads ({})", *number, newest)};
		return {};
	}
} // namespace lain::camera
