#include "lain/io/uri.h"

#include "scheme.h"

#include <filesystem>
#include <system_error>

namespace lain::io
{
	std::string canonicalUri(std::string_view uri)
	{
		const ParsedUri parsed = parseUri(uri);
		if (!isLocalScheme(parsed.scheme))
			return std::string{uri};

		// weakly_canonical rather than canonical: a not-yet-existing path must still have a
		// stable name, since an output pattern is canonicalised before anything is written to it.
		// The error_code overload keeps a permission-denied directory from throwing out of what
		// callers treat as a pure naming function.
		std::error_code error;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::path{parsed.rest}, error);
		if (error)
			return std::string{uri};
		return canonical.string();
	}
} // namespace lain::io
