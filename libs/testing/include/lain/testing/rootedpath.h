#pragma once

// An absolute path that need not exist, for a test of a LEXICAL path rule.
//
// WHY THIS EXISTS. "/projects/hall" is absolute on POSIX and not on Windows, where it has no drive:
// std::filesystem reads it against the working directory's drive, so a test spelling its folder
// that way passes on two platforms and on the third compares "D:/projects/hall/..." against
// "/projects/hall/...". Rooted at this platform's own root, the same test means the same thing
// everywhere, and on POSIX the path is byte for byte what it was.
//
// Header-only and test-only, like the rest of `lain::testing`.

#include <filesystem>
#include <string>

namespace lain::testing
{
	// `rest` under this platform's root: "/projects/hall" on POSIX, "D:\projects/hall" on Windows
	// (whose generic_string() is "D:/projects/hall").
	inline std::filesystem::path rootedPath(const std::string& rest)
	{
		return std::filesystem::absolute("/") / rest;
	}
} // namespace lain::testing
