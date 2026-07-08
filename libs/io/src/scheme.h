#pragma once

// URI scheme parsing shared by io's read and write (private to the library — not a public
// header). A full Uri type would live here if scheme handling ever grows; for now it is a
// bare-path / scheme:// split.

#include <string_view>

namespace lain::io
{
	// The scheme + remainder of a uri. A bare path (no "://") parses as the local scheme with
	// the whole string as the remainder.
	struct ParsedUri
	{
		std::string_view scheme;
		std::string_view rest;
	};

	inline ParsedUri parseUri(std::string_view uri)
	{
		const auto sep = uri.find("://");
		if (sep == std::string_view::npos)
			return {"local", uri};
		return {uri.substr(0, sep), uri.substr(sep + 3)};
	}

	// True when `scheme` names the local filesystem (a bare path or file://). remote/s3 are
	// unsupported for now.
	inline bool isLocalScheme(std::string_view scheme)
	{
		return scheme == "local" || scheme == "file";
	}
} // namespace lain::io
