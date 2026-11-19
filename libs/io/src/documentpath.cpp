#include "lain/io/documentpath.h"

#include <lain/core/uri.h>

#include <system_error>

// Moved from flow::serialize's documentpaths.cpp, expression for expression: the graph documents'
// committed examples are byte-compared with what the builder writes, so a change here would show.
namespace lain::io
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

	// Asked of the stored TEXT, never of a std::filesystem::path built from it (see the header).
	static bool rebasable(const std::string& text)
	{
		return !text.empty() && core::Uri{text}.isLocal();
	}

	std::string relativizePath(const std::string& text, const std::filesystem::path& folder)
	{
		if (!rebasable(text))
			return text;
		const std::filesystem::path path(text);
		std::error_code ec;
		const std::filesystem::path absolute = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
		if (ec)
			return path.generic_string();
		const std::filesystem::path relative = normalPath(absolute).lexically_relative(normalFolder(folder));
		if (relative.empty() || *relative.begin() == "..")
			return absolute.generic_string();
		return relative.generic_string();
	}

	std::string resolvePath(const std::string& text, const std::filesystem::path& folder)
	{
		if (!rebasable(text))
			return text;
		const std::filesystem::path stored(text);
		if (stored.is_absolute())
			return stored.generic_string();
		return normalPath(normalFolder(folder) / stored).generic_string();
	}
} // namespace lain::io
