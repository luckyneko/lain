#pragma once

#include <fmt/args.h>
#include <fmt/format.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lain::string
{
	// What one key captured, per key name.
	using Captures = std::map<std::string, std::string>;

	// The values a Pattern formats from: a name, and a value of any fmt-formattable type.
	//
	// It exists rather than a variadic format(pattern, args...) because the keys are DATA — a
	// caller assembles them from what it happens to know, and a pattern it did not write decides
	// which of them are used. It also keeps fmt out of the call site, which is the whole job of
	// this library.
	class Dictionary
	{
	public:
		// Bind `key` to `value`. Setting one key twice keeps the FIRST value, which is fmt's own
		// named-argument rule rather than one of ours; contains() is how a caller checks.
		template <typename T>
		Dictionary& set(std::string_view key, T value)
		{
			const std::string name{key};
			// fmt copies a std::string into its store but holds a string_view BY REFERENCE
			// (args.h: "string types (but not string views) are copied"), so anything string-like
			// becomes a std::string first. A Dictionary outlives the expression that filled it.
			if constexpr (std::is_convertible_v<T, std::string_view>)
				m_store.push_back(fmt::arg(name.c_str(), std::string{std::string_view{value}}));
			else
				m_store.push_back(fmt::arg(name.c_str(), std::move(value)));
			m_keys.push_back(name);
			return *this;
		}

		bool contains(std::string_view key) const;
		bool empty() const { return m_keys.empty(); }

	private:
		friend class Pattern;
		const fmt::dynamic_format_arg_store<fmt::format_context>& store() const { return m_store; }

		fmt::dynamic_format_arg_store<fmt::format_context> m_store;
		std::vector<std::string> m_keys; // which tokens format() may rewrite
	};

	// Literal text with named holes: "shot.<frame:04>.png", "<take>/<camera>.mov".
	//
	// It replaces io::NumberField, which could express exactly one thing — where a number goes,
	// spelled "####". The mechanism is a rewrite: a key the dictionary holds becomes "{key:spec}"
	// and the whole thing goes through ONE fmt::vformat, so the spec after ':' is fmt's own
	// format-spec language with almost no parsing here. A key the dictionary does NOT hold
	// survives as its own token, which is what makes partial resolution work — a half-filled
	// pattern is still a pattern.
	//
	// THE GRAMMAR IS "<" NAME (":" SPEC)? ">", NAME being [A-Za-z_][A-Za-z0-9_]* and SPEC running
	// to the closing '>' (see the alignment rule below) and holding no brace. Anything that is not
	// a well-formed token is LITERAL text, so construction is total and never fails — "<3 files>"
	// stays as written because a name cannot hold a space, and that doubles as the escape.
	//
	// THE SPEC IS FMT'S, WHOLE, including the two alignments spelled with this grammar's own
	// delimiters. fmt puts the alignment at spec index 0 (bare) or 1 (after a fill) and nowhere
	// else, so a '<' or '>' THERE is part of the spec and a later '>' ends the token:
	// "<frame:>8>" right-aligns, "<frame:*<8>" pads with '*' on the right. An alignment is only
	// assumed when that reading still leaves a closing '>' behind — "<name:8>" is a width of 8,
	// not a fill of '8' — so the rule adds spellings without changing any that already worked.
	// What stays ambiguous is a spec followed by literal text holding a '>' ("<a:b>c>" reads as
	// fill 'b' aligned right), which is fmt's own reading of it.
	//
	// '<' and '>' were chosen over '[' and ']' on evidence, and re-confirmed once the alignment
	// rule above removed the one thing brackets would have bought. A bracketed pattern is a GLOB:
	// unquoted in a shell, "out.[frame:04].png" can expand to an existing "out.4.png" — silently
	// the wrong file, where '<' and '>' fail loudly as redirections. YAML misparses a bare scalar
	// beginning with '[' as a flow sequence (and data/reader.h names a future YamlReader).
	// "<UDIM>" / "<f>" is the convention every renderer already uses. And neither of these two is
	// legal in a Windows filename, so a real file can never be mistaken for a pattern.
	//
	// IT IS NOT A REGEX ENGINE, and match() is where that shows. A key captures the shortest
	// non-empty run that lets the rest of the pattern match, so two adjacent keys are ambiguous and
	// are resolved — not refused — by the earlier one taking the shorter capture. The cost of a
	// match is exponential in the number of keys, which is nothing for a filename and would be
	// wrong for a large body of text.
	class Pattern
	{
	public:
		explicit Pattern(std::string_view text);

		// The pattern as written, for a caller's own diagnostics.
		const std::string& text() const { return m_text; }

		// The key names, in order of first appearance, without repeats.
		const std::vector<std::string>& keys() const { return m_keys; }
		bool has(std::string_view key) const;

		// The literals, plus each key `values` holds. Returns nullopt when fmt refuses a spec —
		// "<frame:zz>" — which is a caller's text and so a caller's error to report. lain::string
		// cannot log (the dependency to lain::log runs one way), so the refusal is the return type.
		std::optional<std::string> format(const Dictionary& values) const;

		// `text` matched WHOLE against this pattern: every literal exactly, every key non-empty,
		// and nothing left over. A key repeated in the pattern must capture the same text twice.
		//
		// The spec is deliberately not consulted. Matching is delimited by the literals around a
		// key and nothing else, so "shot.<frame:04>.png" captures "0007", "7" and "x" alike — what
		// a capture MEANS belongs to the caller, which has to parse it anyway.
		std::optional<Captures> match(std::string_view text) const;

	private:
		// A literal run, then the key that follows it. The last piece carries the trailing literal
		// and an empty key, so a walk over the pieces needs no special case for either end.
		struct Piece
		{
			std::string literal;
			std::string key;
			std::string spec;
		};

		bool matchFrom(std::size_t index, std::string_view text, std::size_t pos, Captures& captures) const;

		std::string m_text;
		std::vector<Piece> m_pieces;
		std::vector<std::string> m_keys;
	};
} // namespace lain::string
