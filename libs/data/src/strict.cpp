#include "lain/data/strict.h"

namespace lain::data
{
	static std::string join(const std::string& path, const std::string& key)
	{
		return path.empty() ? key : path + "." + key;
	}

	static std::string quoted(const std::string& path)
	{
		return path.empty() ? "the document" : "'" + path + "'";
	}

	static const char* kindOf(const Value& value)
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

	static void compare(const Value& stored, const Value& reencoded, const std::string& path,
						std::vector<std::string>& problems)
	{
		if (std::string(kindOf(stored)) != kindOf(reencoded))
		{
			problems.push_back(quoted(path) + " is " + kindOf(stored) + ", where " + kindOf(reencoded) + " belongs");
			return;
		}
		if (const Value::Object* entries = stored.asObject())
		{
			for (const auto& [key, value] : *entries)
			{
				if (const Value* counterpart = reencoded.find(key))
					compare(value, *counterpart, join(path, key), problems);
				else
					problems.push_back("'" + join(path, key) + "' is not a known key");
			}
			for (const auto& [key, value] : *reencoded.asObject())
			{
				(void)value;
				if (!stored.find(key))
					problems.push_back("'" + join(path, key) + "' is missing");
			}
		}
		else if (const Value::Array* items = stored.asArray())
		{
			const Value::Array& others = *reencoded.asArray();
			if (items->size() != others.size())
			{
				problems.push_back(quoted(path) + " has " + std::to_string(items->size()) + " entries, and " +
								   std::to_string(others.size()) + " were read");
				return;
			}
			for (std::size_t i = 0; i < items->size(); ++i)
				compare((*items)[i], others[i], path + "[" + std::to_string(i) + "]", problems);
		}
		else if (stored.isString() && *stored.asString() != *reencoded.asString())
		{
			problems.push_back(quoted(path) + " is '" + *stored.asString() + "', which was not recognised");
		}
	}

	std::vector<std::string> shapeDifferences(const Value& stored, const Value& reencoded)
	{
		std::vector<std::string> problems;
		compare(stored, reencoded, "", problems);
		return problems;
	}
} // namespace lain::data
