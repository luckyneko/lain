#pragma once

#include <cstddef>
#include <string_view>

// Type-level introspection for lain::meta: typeName / typeNameShort, type-level so they
// sit directly in lain::meta (the enum value functions live under lain::meta::enums).
//
// Names come from the compiler's signature intrinsic (__FUNCSIG__ / __PRETTY_FUNCTION__)
// parsed at compile time — owned, no external dep, and a clean string_view into static
// storage on every compiler (no RTTI / demangling). They are human/debug-facing (port
// labels, log lines), not stable serialization keys.
namespace lain::meta
{
	namespace detail
	{
		// The enclosing function's signature, with T spelled out by the compiler.
		template <typename T>
		constexpr std::string_view signature()
		{
#if defined(_MSC_VER)
			return __FUNCSIG__;
#else
			return __PRETTY_FUNCTION__;
#endif
		}
	} // namespace detail

	// Fully-qualified type name, e.g. typeName<lain::core::Version>() == "lain::core::Version".
	template <typename T>
	constexpr std::string_view typeName()
	{
		// Probe with a known type (void) to measure the fixed prefix/suffix the compiler
		// wraps the type in, then slice the same window out of signature<T>() — robust to
		// the exact signature format without hardcoding per-compiler offsets.
		constexpr std::string_view probe = detail::signature<void>();
		constexpr std::size_t start = probe.find("void");
		constexpr std::size_t suffix = probe.size() - start - 4; // 4 == length of "void"

		std::string_view name = detail::signature<T>();
		name = name.substr(start, name.size() - start - suffix);

		// MSVC spells class/struct/enum/union types with the keyword in the signature;
		// drop it so the name reads the same as on GCC/Clang.
		if (name.substr(0, 6) == "class ")
			name.remove_prefix(6);
		else if (name.substr(0, 7) == "struct ")
			name.remove_prefix(7);
		else if (name.substr(0, 5) == "enum ")
			name.remove_prefix(5);
		else if (name.substr(0, 6) == "union ")
			name.remove_prefix(6);
		return name;
	}

	// Unqualified type name (no namespace), e.g. typeNameShort<lain::core::Version>() ==
	// "Version" — trimmed to everything after the last "::" of the full name. (A
	// templated type keeps its argument list; the last "::" inside the arguments would be
	// trimmed, so prefer this for plain types.)
	template <typename T>
	constexpr std::string_view typeNameShort()
	{
		const std::string_view full = typeName<T>();
		const auto pos = full.rfind("::");
		return pos == std::string_view::npos ? full : full.substr(pos + 2);
	}
} // namespace lain::meta
