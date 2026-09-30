#include "lain/flow/serialize/documentpaths.h"

#include <lain/core/uri.h>

#include <string>
#include <string_view>
#include <system_error>

// All of this is LEXICAL. Nothing reads the file system: whether a path is "inside" a folder is a
// question about spellings, answered the same way for a file that does not exist yet. Both sides are
// made absolute and lexically normal, and neither is canonicalised, since resolving symlinks on one
// side only (/var against /private/var on macOS) would put a file "outside" the folder it is in.
namespace lain::flow::serialize
{
	// A lexically normal path without a trailing separator. lexically_normal keeps the separator a
	// "." leaves behind ("/a/b/." -> "/a/b/"), and a folder needs ONE spelling, or save -> load ->
	// save stops being byte-stable.
	static std::filesystem::path normalPath(const std::filesystem::path& path)
	{
		std::filesystem::path normal = path.lexically_normal();
		if (!normal.has_filename() && normal.has_relative_path())
			normal = normal.parent_path();
		return normal;
	}

	// `folder` made absolute (against the working directory) and normal.
	static std::filesystem::path normalFolder(const std::filesystem::path& folder)
	{
		std::error_code ec;
		const std::filesystem::path absolute =
			folder.empty() ? std::filesystem::current_path(ec) : std::filesystem::absolute(folder, ec);
		return ec ? normalPath(folder) : normalPath(absolute);
	}

	// An empty path ("no file chosen yet") and a uri with a scheme are not file-system paths to
	// rebase. Leaving them alone is what keeps both transforms total: rebasing an empty path would
	// name the document's own folder.
	static bool rebasable(const std::filesystem::path& path)
	{
		return !path.empty() && core::Uri{path.generic_string()}.isLocal();
	}

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
					*value = data::Value(rewrite(std::filesystem::path(*value->asString())).generic_string());
				}
			}
			if (data::Value* inner = member(node, "graph"))
				rewritePathParams(*inner, rewrite);
		}
	}

	void relativizePaths(data::Value& document, const std::filesystem::path& folder)
	{
		const std::filesystem::path base = normalFolder(folder);
		rewritePathParams(document,
						  [&base](const std::filesystem::path& path)
						  {
							  if (!rebasable(path))
								  return path;
							  // A relative in-memory path is the working directory's, since that is
							  // what the OS reads it against. Written as it is, a load would read it
							  // against the document instead: a different file, with nothing to say so.
							  std::error_code ec;
							  const std::filesystem::path absolute =
								  path.is_absolute() ? path : std::filesystem::absolute(path, ec);
							  if (ec)
								  return path;
							  // Outside the tree stays absolute, so a document moved on its own still
							  // finds footage kept elsewhere; a `../` chain would break exactly then.
							  const std::filesystem::path relative = normalPath(absolute).lexically_relative(base);
							  if (relative.empty() || *relative.begin() == "..")
								  return absolute;
							  return relative;
						  });
	}

	void resolvePaths(data::Value& document, const std::filesystem::path& folder)
	{
		const std::filesystem::path base = normalFolder(folder);
		rewritePathParams(document,
						  [&base](const std::filesystem::path& stored)
						  {
							  if (!rebasable(stored) || stored.is_absolute())
								  return stored;
							  return normalPath(base / stored);
						  });
	}
} // namespace lain::flow::serialize
