#include "lain/flow/serialize/documentpaths.h"

#include <lain/io/documentpath.h> // the rule for one path, which every document format shares

#include <string>
#include <string_view>
#include <system_error>

// The rule for one path is lain::io's (io/documentpath.h, ADR-0027); what is a graph document's own
// is where its paths are: a path param's value, in a document's nodes and every nested body.
namespace lain::flow::serialize
{
	// A member of an object Value, mutably. data::Value::find is const-only.
	static data::Value* member(data::Value& object, std::string_view key)
	{
		data::Value::Object* entries = object.asObject();
		if (!entries)
			return nullptr;
		for (auto& [name, value] : *entries)
		{
			if (name == key)
				return &value;
		}
		return nullptr;
	}

	// Rewrite, through `rewrite`, the value of every path param in a document BODY: its own nodes and
	// every nested body (an inline group's, a map's, a loop's). A linked group has no nested body: its
	// template is a document of its own, transformed when the host reads it.
	template <typename Rewrite>
	static void rewritePathParams(data::Value& body, const Rewrite& rewrite)
	{
		data::Value* nodes = member(body, "nodes");
		if (!nodes || !nodes->asArray())
			return;
		for (data::Value& node : *nodes->asArray())
		{
			if (data::Value* params = member(node, "params"); params && params->asArray())
			{
				for (data::Value& param : *params->asArray())
				{
					const data::Value* type = param.find("type");
					data::Value* value = member(param, "value");
					if (!type || !type->asString() || *type->asString() != kPathTypeKey || !value || !value->asString())
						continue;
					*value = data::Value(rewrite(*value->asString()));
				}
			}
			if (data::Value* inner = member(node, "graph"))
				rewritePathParams(*inner, rewrite);
		}
	}

	void relativizePaths(data::Value& document, const std::filesystem::path& folder)
	{
		rewritePathParams(document, [&folder](const std::string& text)
						  { return io::relativizePath(text, folder); });
	}

	void resolvePaths(data::Value& document, const std::filesystem::path& folder)
	{
		rewritePathParams(document, [&folder](const std::string& text)
						  { return io::resolvePath(text, folder); });
	}
} // namespace lain::flow::serialize
