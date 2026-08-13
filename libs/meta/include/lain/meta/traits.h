#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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

	// Structural "is a specialization of std::X" traits (std::type_traits style). Generic — not
	// tied to any one consumer (lain::data's serialization dispatch is the first). Add more std-shape
	// traits here as a real consumer appears.
	template <typename T>
	struct is_optional : std::false_type
	{
	};
	template <typename U>
	struct is_optional<std::optional<U>> : std::true_type
	{
	};
	template <typename T>
	inline constexpr bool is_optional_v = is_optional<T>::value;

	template <typename T>
	struct is_vector : std::false_type
	{
	};
	template <typename U, typename A>
	struct is_vector<std::vector<U, A>> : std::true_type
	{
	};
	template <typename T>
	inline constexpr bool is_vector_v = is_vector<T>::value;

	// What is INSIDE a std::vector — `void` for anything that isn't one. The other half of
	// is_vector: a consumer that has detected a vector almost always needs to name its element
	// type next, and deriving that separately at each site is how the two drift apart.
	// (lain::flow's collection capability is the first caller: it splits a vector<E> value into
	// Es for a map node's children, and gathers Es back into a vector<E>.)
	template <typename T>
	struct vector_element
	{
		using type = void;
	};
	template <typename U, typename A>
	struct vector_element<std::vector<U, A>>
	{
		using type = U;
	};
	template <typename T>
	using vector_element_t = typename vector_element<T>::type;

	template <typename T>
	struct is_variant : std::false_type
	{
	};
	template <typename... Ts>
	struct is_variant<std::variant<Ts...>> : std::true_type
	{
	};
	template <typename T>
	inline constexpr bool is_variant_v = is_variant<T>::value;
} // namespace lain::meta
