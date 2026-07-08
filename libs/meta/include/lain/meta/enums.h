#pragma once

#include <magic_enum/magic_enum.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

// lain::meta — compile-time type/enum introspection behind a lain:: face. Enum
// reflection lives in the lain::meta::enums sub-namespace (mirroring this header) so
// the short names stay clear of the type-level introspection (typeName, constexpr type
// traits) intended to join lain::meta later. magic_enum is named nowhere past here —
// consumers see only lain::meta::enums.
//
// Every function is enum-only (a static_assert rejects non-enum E with a clear error).
// Limitation inherited from magic_enum: only enumerators in [MAGIC_ENUM_RANGE_MIN,
// MAGIC_ENUM_RANGE_MAX] (default [-128, 128]) are seen — fine for lain's small enums,
// but a flag/bitmask enum with large values needs the range customized.
namespace lain::meta::enums
{
	// The enumerator's name (empty for an unnamed value). Backed by static storage.
	template <typename E>
	std::string_view name(E value)
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::name requires an enum type");
		return magic_enum::enum_name(value);
	}

	// Parse a name back to its enumerator (nullopt if it matches none). Case-sensitive;
	// pass caseInsensitive=true to ignore case.
	template <typename E>
	std::optional<E> fromString(std::string_view text, bool caseInsensitive = false)
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::fromString requires an enum type");
		if (caseInsensitive)
			return magic_enum::enum_cast<E>(text, magic_enum::case_insensitive);
		return magic_enum::enum_cast<E>(text);
	}

	// The number of named enumerators.
	template <typename E>
	constexpr std::size_t count()
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::count requires an enum type");
		return magic_enum::enum_count<E>();
	}

	// All enumerator values, in declaration order: std::array<E, count<E>()>.
	template <typename E>
	constexpr auto values()
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::values requires an enum type");
		return magic_enum::enum_values<E>();
	}

	// All enumerator names, in declaration order: std::array<string_view, count<E>()>.
	template <typename E>
	constexpr auto names()
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::names requires an enum type");
		return magic_enum::enum_names<E>();
	}

	// (value, name) pairs, in declaration order: std::array<std::pair<E, string_view>,
	// count<E>()> — the constexpr building block behind the maps below.
	template <typename E>
	constexpr auto entries()
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::entries requires an enum type");
		return magic_enum::enum_entries<E>();
	}

	// A name -> value map (canonical names). The building block for an idiomatic CLI11
	// option (feed it to cli::CheckedTransformer) or any name-keyed lookup. Allocating
	// — runtime, unlike the constexpr accessors above.
	template <typename E>
	std::map<std::string, E> nameValueMap()
	{
		static_assert(std::is_enum_v<E>, "lain::meta::enums::nameValueMap requires an enum type");
		std::map<std::string, E> map;
		for (const auto& [value, label] : magic_enum::enum_entries<E>())
			map.emplace(std::string(label), value);
		return map;
	}
} // namespace lain::meta::enums
