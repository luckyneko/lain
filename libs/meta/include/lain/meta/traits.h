#pragma once

#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

// Constexpr type-traits for lain::meta — capability detection for generic code, in
// std::type_traits style (snake_case + a _v variable). Pure std: no magic_enum / nameof,
// so this header is cheap to include. Add traits here as a real consumer appears, not
// speculatively. These sit directly in lain::meta (type-level), beside typeName.
namespace lain::meta
{
	// True when T exposes `toString()` returning something convertible to std::string.
	// Backs lain::string's fmt formatter — a type becomes formattable just by having it.
	// (Safe to gate a global formatter on: a string-returning toString() is rare, so it
	// doesn't collide with fmt's own type coverage.)
	template <typename T, typename = void>
	struct has_to_string : std::false_type
	{
	};

	template <typename T>
	struct has_to_string<T, std::void_t<decltype(std::declval<const T&>().toString())>>
		: std::is_convertible<decltype(std::declval<const T&>().toString()), std::string>
	{
	};

	template <typename T>
	inline constexpr bool has_to_string_v = has_to_string<T>::value;

	// True when `os << value` compiles for `std::ostream& os` and `const T& value` — T
	// has a stream insertion operator. A detection primitive for surgical use; do NOT
	// turn it into a global fmt formatter: built-ins (int, std::string, …) are
	// stream-able, so such a formatter would be ambiguous with fmt's own formatters.
	template <typename T, typename = void>
	struct has_ostream : std::false_type
	{
	};

	template <typename T>
	struct has_ostream<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
		: std::true_type
	{
	};

	template <typename T>
	inline constexpr bool has_ostream_v = has_ostream<T>::value;
} // namespace lain::meta
