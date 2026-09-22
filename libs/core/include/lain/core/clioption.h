#pragma once

#include "lain/core/parse.h" // core::parseInto — the body the macro expands to

#include <string>

// Declaring a lain value type as a COMMAND-LINE OPTION TYPE.
//
// CLI11 converts a custom option type through an unqualified `lexical_cast(const std::string&, T&)`
// that it finds by ADL on T. Two consequences shape this header:
//
//   - The hook must sit in the TYPE'S OWN namespace, so it cannot be declared once for every type
//     the way lain::string's fmt formatter is — ADL is what forbids that. It is one line per type,
//     beside the type.
//   - The hook is a PLAIN SIGNATURE, so nothing here names CLI11. That is what lets a core or media
//     type offer it while CLI11 stays a dependency of lain::app alone.
//
// The macro exists so that line reads as what it IS. Spelled out, `bool lexical_cast(const
// std::string&, FrameRate&);` is an anonymous-looking free function with nothing to connect it to a
// command line, and it was written out twice, identically, before this header existed.
//
//   namespace lain::media
//   {
//       struct FrameRate { ... static std::optional<FrameRate> parse(std::string_view); };
//
//       LAIN_CLI_OPTION(FrameRate)   // at namespace scope, beside the type
//   }
//
// The whole hook is here: declaration and body in one place, so there is no .cpp half to keep in
// step and no second spelling of the signature. That is the trade this header makes — a caller
// takes core/parse.h into its public header (std-only, and the type is already about parsing) and
// gets one line per type with nothing to synchronise. No trailing semicolon, as LAIN_SERIALIZE.
//
// T must provide `static std::optional<T> parse(std::string_view)`. A type wanting different
// behaviour writes the function by hand instead; the macro is a convenience, never the only route.
//
// NOT A TEMPLATE, deliberately. One constrained template plus a per-namespace using-declaration
// does work — ADL does reach a function introduced by a using-declaration, which was measured, not
// assumed — but at two types it saves nothing, trades a greppable per-type line for an implicit
// "any type here with a parse() is bindable" rule, and leaves a using-declaration that looks
// deletable and silently breaks conversion when deleted. Revisit at a third bindable type, or a
// bindable type in a third namespace.

#define LAIN_CLI_OPTION(Type)                                        \
	inline bool lexical_cast(const std::string& input, Type& output) \
	{                                                                \
		return ::lain::core::parseInto(input, output);               \
	}
