#pragma once

#include <nameof.hpp>

#include <string_view>

// Type-level introspection for lain::meta. typeName lives directly in lain::meta
// (type-level, unlike the enum value functions under lain::meta::enums), so the
// type-introspection surface — typeName now, constexpr type traits later — reads as
// lain::meta::. nameof is named nowhere past here.
//
// The names come from compiler intrinsics (__PRETTY_FUNCTION__ / __FUNCSIG__) that
// nameof normalizes across compilers; treat them as human/debug-facing (port-type
// labels, log lines), not as stable serialization keys.
namespace lain::meta
{
	// Fully-qualified type name, e.g. typeName<lain::core::Version>() == "lain::core::Version".
	template <typename T>
	constexpr std::string_view typeName()
	{
		return nameof::nameof_type<T>();
	}

	// Unqualified type name (no namespace), e.g. typeNameShort<lain::core::Version>() ==
	// "Version". Derived by trimming everything up to the last "::" of the full name —
	// nameof's own short-name parser misfires on some toolchains, and the full name is
	// reliable. (A templated type keeps its argument list; the last "::" inside the
	// arguments would be trimmed, so prefer this for plain types.)
	template <typename T>
	constexpr std::string_view typeNameShort()
	{
		const std::string_view full = typeName<T>();
		const auto pos = full.rfind("::");
		return pos == std::string_view::npos ? full : full.substr(pos + 2);
	}
} // namespace lain::meta
