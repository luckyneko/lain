#include "lain/io/uri.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace lain::io
{
	lain::core::Uri canonicalise(const lain::core::Uri& uri)
	{
		// The scheme is asked about ONCE, by Uri::path(): a uri it can turn into a path is one this
		// library can serve, and one it cannot has nothing to canonicalise. Testing isLocal()
		// separately would be a second place deciding the same thing.
		const std::optional<std::filesystem::path> local = uri.path();
		if (!local.has_value())
			return uri;

		// weakly_canonical rather than canonical: a not-yet-existing path must still have a
		// stable name, since an output pattern is canonicalised before anything is written to it.
		// The error_code overload keeps a permission-denied directory from throwing out of what
		// callers treat as a pure naming function.
		std::error_code error;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(*local, error);
		if (error)
			return uri;
		return lain::core::Uri::fromPath(canonical);
	}

	NumberField numberField(std::string_view pattern)
	{
		const std::size_t start = pattern.find('#');
		if (start == std::string_view::npos)
			return {};

		std::size_t end = start;
		while (end < pattern.size() && pattern[end] == '#')
			++end;
		return NumberField{start, end - start};
	}

	std::string substituteNumber(std::string_view pattern, unsigned long long number)
	{
		const NumberField field = numberField(pattern);
		if (!field.found())
			return std::string{pattern};

		std::string digits = std::to_string(number);
		// Pad up to the field's width, but never truncate past it: "####" at frame 10000 widens to
		// five digits rather than losing one. numberField's matcher accepts any run of digits, so a
		// widened name is still found when the sequence is read back.
		if (digits.size() < field.width)
			digits.insert(digits.begin(), field.width - digits.size(), '0');

		std::string out{pattern.substr(0, field.offset)};
		out += digits;
		out += pattern.substr(field.offset + field.width);
		return out;
	}
} // namespace lain::io
