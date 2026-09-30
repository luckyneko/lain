#include "strict.h"

#include <lain/string/format.h>

namespace lain::camera::detail
{
	static std::string join(const std::string& path, const std::string& key)
	{
		return path.empty() ? key : path + "." + key;
	}

	static const char* kindOf(const data::Value& value)
	{
		if (value.isNull())
			return "null";
		if (value.isBool())
			return "a boolean";
		if (value.isNumber())
			return "a number";
		if (value.isString())
			return "a string";
		if (value.isBytes())
			return "bytes";
		if (value.isArray())
			return "an array";
		return "an object";
	}

	static bool sameKind(const data::Value& a, const data::Value& b)
	{
		return (a.isNumber() && b.isNumber()) || std::string(kindOf(a)) == kindOf(b);
	}

	std::vector<std::string> shapeDifferences(const data::Value& stored, const data::Value& reencoded,
											  const std::string& path)
	{
		std::vector<std::string> problems;
		const std::string where = path.empty() ? "the document" : "'" + path + "'";
		if (!sameKind(stored, reencoded))
		{
			problems.push_back(lain::string::format("{} is {}, where {} belongs", where, kindOf(stored), kindOf(reencoded)));
			return problems;
		}
		if (const data::Value::Object* entries = stored.asObject())
		{
			for (const auto& [key, value] : *entries)
			{
				const data::Value* counterpart = reencoded.find(key);
				if (!counterpart)
				{
					problems.push_back(lain::string::format("'{}' is not a known key", join(path, key)));
					continue;
				}
				const std::vector<std::string> nested = shapeDifferences(value, *counterpart, join(path, key));
				problems.insert(problems.end(), nested.begin(), nested.end());
			}
			for (const auto& [key, value] : *reencoded.asObject())
			{
				(void)value;
				if (!stored.find(key))
					problems.push_back(lain::string::format("'{}' is missing", join(path, key)));
			}
		}
		else if (const data::Value::Array* items = stored.asArray())
		{
			const data::Value::Array& others = *reencoded.asArray();
			if (items->size() != others.size())
				problems.push_back(lain::string::format("{} has {} entries, and {} were read", where, items->size(), others.size()));
			else
			{
				for (std::size_t i = 0; i < items->size(); ++i)
				{
					const std::vector<std::string> nested =
						shapeDifferences((*items)[i], others[i], lain::string::format("{}[{}]", path, i));
					problems.insert(problems.end(), nested.begin(), nested.end());
				}
			}
		}
		else if (stored.isString() && *stored.asString() != *reencoded.asString())
		{
			problems.push_back(lain::string::format("{} is '{}', which was not recognised", where, *stored.asString()));
		}
		return problems;
	}
} // namespace lain::camera::detail
