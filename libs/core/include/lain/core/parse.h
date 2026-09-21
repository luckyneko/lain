#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace lain::core
{
	// Text to a number, the two ways the tree actually asks for it.
	//
	// `parse` is the verb Version, Range, Uuid and Uri already use for "text to a value, or
	// nothing", and this is the primitive all four are built out of — so it is the same verb applied
	// one level down, not a new one. It is deliberately NOT a fifth io verb (CONTEXT.md, The four
	// verbs): those name what a LOADING function hands back, and this sits below all of them.
	//
	// Not reachable by ADL: a std::string_view associates namespace std, so a caller writes
	// core::parse<T>(text) or brings the name in deliberately. That is the property image::descriptor
	// lacked when it was a bare noun found from any scope holding a PixelFormat.
	//
	// ONE POLICY, two mechanisms. An integral type reads through std::from_chars: no locale, no
	// allocation, no throw; it reports result_out_of_range rather than wrapping to a small number,
	// and it skips no whitespace and accepts no sign for an unsigned type, so " 1", "+1" and "-1"
	// are refused with no check of our own. A FLOATING-POINT type cannot use it: libc++ marks the
	// floating-point overloads "introduced in macOS 26", so on Apple they are unavailable to
	// anything with an ordinary deployment target — not a toolchain to wait for. That arm reads
	// through std::strtof / strtod / strtold instead, and the spellings strtof accepts and
	// from_chars does not are refused explicitly, so WHICH MECHANISM RAN IS NEVER VISIBLE IN THE
	// ANSWER. That the two agree is pinned by a test, not by this paragraph.
	//
	// The floating arm's two costs, stated rather than discovered: it is LOCALE-DEPENDENT through
	// LC_NUMERIC (so a decimal-comma locale would misread "1.5" — the same exposure std::stof always
	// had, and lain never calls std::setlocale), and it copies the field to null-terminate it
	// (inside std::string's small buffer for any real number). Both end with the mechanism: when
	// from_chars is reachable, that arm is deleted and no caller changes, which is the whole reason
	// this lives in core rather than at each call site.

	// A leading run of digits at `pos`, with `pos` advanced past it. nullopt when there is not one
	// there, or when the value would not fit — refusing beats wrapping, since a wrapped value is a
	// wrong answer that says nothing about being wrong. `pos` does not move on a refusal.
	template <typename T>
	std::optional<T> parseAt(std::string_view text, std::size_t& pos);

	// The WHOLE text as T, or nothing. This IS parseAt, plus: and nothing was left over — which is
	// what rejects "1x", since from_chars stops at the first non-digit and reports success for the
	// prefix it did read. A caller wanting that looser reading asks parseAt and says so.
	template <typename T>
	std::optional<T> parse(std::string_view text);
} // namespace lain::core

// Both are templates, and a body is implementation. Split out for the reason details/uri.inl is.
#include "lain/core/details/parse.inl"
