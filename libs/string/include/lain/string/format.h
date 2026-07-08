#pragma once

#include <fmt/format.h>
#include <lain/meta/traits.h> // lain::meta::has_to_string

#include <string>
#include <type_traits>
#include <utility>

// A generic fmt formatter for any type exposing `toString()` (lain::meta::has_to_string):
// it renders through that and reuses fmt's std::string formatter, so format specs
// (width / fill / align) still apply. Defined here, not in lain::core, so the fmt
// dependency stays out of core and a type becomes formattable just by having
// `toString()` — no per-type registration.
//
// This is a partial specialization of fmt::formatter gated by the trait, so types
// without `toString()` fall through to fmt's own formatters. (Caveat: a type that is
// *both* a range/tuple — which fmt formats via its own partial specialization — and
// has `toString()` would make the two ambiguous; none of lain's do.)
template <typename T>
struct fmt::formatter<T, char, std::enable_if_t<lain::meta::has_to_string<T>::value>>
	: fmt::formatter<std::string>
{
	auto format(const T& value, fmt::format_context& ctx) const
	{
		return fmt::formatter<std::string>::format(value.toString(), ctx);
	}
};

// lain::string — string utilities behind a lain:: face. For now: format(), a wrapper
// over fmt::format (C++17 has no std::format). split / join / trim / case helpers can
// land here when a caller needs them.
namespace lain::string
{
	// Build a std::string from a compile-time-checked format string and its arguments.
	// Any argument exposing `toString()` renders via the formatter above; everything
	// else uses fmt's own formatters. For a runtime/dynamic format, reach for
	// fmt::vformat directly — this front-end is the checked path.
	template <typename... Args>
	std::string format(fmt::format_string<Args...> fmtStr, Args&&... args)
	{
		return fmt::format(fmtStr, std::forward<Args>(args)...);
	}
} // namespace lain::string
