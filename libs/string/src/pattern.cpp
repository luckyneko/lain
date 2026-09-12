#include "lain/string/pattern.h"

#include <algorithm>

namespace lain::string
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Spelled out rather than std::isalpha / std::isdigit, which are locale-dependent: a key name
	// must mean the same thing on every machine that reads the pattern.
	static bool isNameStart(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	}

	static bool isNameChar(char c)
	{
		return isNameStart(c) || (c >= '0' && c <= '9');
	}

	// fmt's alignment characters, mirroring fmt::detail::parse_align. They matter here because two
	// of them are this grammar's own delimiters: '>' ends a token, so without knowing where an
	// alignment may legally sit, "{:>8}" and "{:*<8}" would have no spelling at all.
	static bool isAlign(char c)
	{
		return c == '<' || c == '>' || c == '^';
	}

	// The first '>' at or after `from` that closes a token, or npos. A brace or a '<' stops the
	// scan without closing anything: the token is emitted verbatim when its key is unmapped, and a
	// brace in it would then reach fmt as a format hole nobody wrote.
	static std::size_t scanToClose(std::string_view text, std::size_t from)
	{
		std::size_t i = from;
		while (i < text.size() && text[i] != '>' && text[i] != '<' && text[i] != '{' && text[i] != '}')
			++i;
		return (i < text.size() && text[i] == '>') ? i : std::string_view::npos;
	}

	// Where the spec starting at `from` ends — the one place this grammar knows anything about what
	// a spec MEANS.
	//
	// fmt puts an alignment at spec index 0 (bare) or 1 (after a fill) and nowhere else, so a '<'
	// or '>' THERE belongs to the spec: without this, "{:>8}" and "{:*<8}" would have no spelling
	// at all, since '>' is also what ends a token. But '>' cannot be both blindly — "<n:8>" is a
	// width of 8, not a fill of '8' — so an alignment is only assumed when that reading still
	// leaves a closing '>' behind. Longest first, falling back, which is what makes the rule purely
	// additive: no pattern that read one way before reads another way now.
	static std::size_t findSpecEnd(std::string_view text, std::size_t from)
	{
		if (from + 1 < text.size() && isAlign(text[from + 1])) // fill, then align
		{
			const std::size_t end = scanToClose(text, from + 2);
			if (end != std::string_view::npos)
				return end;
		}
		if (from < text.size() && isAlign(text[from])) // bare align
		{
			const std::size_t end = scanToClose(text, from + 1);
			if (end != std::string_view::npos)
				return end;
		}
		return scanToClose(text, from);
	}

	// The length of a well-formed "<name>" / "<name:spec>" starting at `pos`, or 0 when there is
	// none — which is how a stray '<' becomes literal text rather than an error.
	static std::size_t readToken(std::string_view text, std::size_t pos, std::string& key, std::string& spec)
	{
		if (pos >= text.size() || text[pos] != '<')
			return 0;

		std::size_t i = pos + 1;
		if (i >= text.size() || !isNameStart(text[i]))
			return 0;

		const std::size_t nameStart = i;
		while (i < text.size() && isNameChar(text[i]))
			++i;
		const std::size_t nameEnd = i;

		std::size_t specStart = i;
		std::size_t specEnd = i;
		if (i < text.size() && text[i] == ':')
		{
			specStart = ++i;
			specEnd = findSpecEnd(text, specStart);
			if (specEnd == std::string_view::npos)
				return 0;
			i = specEnd;
		}

		if (i >= text.size() || text[i] != '>')
			return 0;

		key = std::string{text.substr(nameStart, nameEnd - nameStart)};
		spec = std::string{text.substr(specStart, specEnd - specStart)};
		return i + 1 - pos;
	}

	// Literal text as a fmt format string: a brace means itself, so it is doubled.
	static void appendLiteral(std::string& out, std::string_view literal)
	{
		for (const char c : literal)
		{
			if (c == '{' || c == '}')
				out.push_back(c);
			out.push_back(c);
		}
	}

	static void appendKey(std::string& out, char open, const std::string& key, const std::string& spec, char close)
	{
		out.push_back(open);
		out += key;
		if (!spec.empty())
		{
			out.push_back(':');
			out += spec;
		}
		out.push_back(close);
	}

	// --- Dictionary --------------------------------------------------------------

	bool Dictionary::contains(std::string_view key) const
	{
		return std::find(m_keys.begin(), m_keys.end(), key) != m_keys.end();
	}

	// --- Pattern -----------------------------------------------------------------

	Pattern::Pattern(std::string_view text)
		: m_text(text)
	{
		Piece piece;
		std::string key;
		std::string spec;

		for (std::size_t i = 0; i < m_text.size();)
		{
			const std::size_t length = readToken(m_text, i, key, spec);
			if (length == 0)
			{
				piece.literal.push_back(m_text[i]);
				++i;
				continue;
			}

			piece.key = key;
			piece.spec = spec;
			m_pieces.push_back(std::move(piece));
			piece = Piece{};

			if (std::find(m_keys.begin(), m_keys.end(), key) == m_keys.end())
				m_keys.push_back(key);
			i += length;
		}

		// The trailing literal, always present even when empty — it is what makes match() able to
		// ask "and nothing left over" at the same place it matches every other literal.
		m_pieces.push_back(std::move(piece));
	}

	bool Pattern::has(std::string_view key) const
	{
		return std::find(m_keys.begin(), m_keys.end(), key) != m_keys.end();
	}

	std::optional<std::string> Pattern::format(const Dictionary& values) const
	{
		std::string form;
		form.reserve(m_text.size() + 8);

		for (const Piece& piece : m_pieces)
		{
			appendLiteral(form, piece.literal);
			if (piece.key.empty())
				continue;

			if (values.contains(piece.key))
				appendKey(form, '{', piece.key, piece.spec, '}');
			else
				appendKey(form, '<', piece.key, piece.spec, '>'); // survives as itself
		}

		try
		{
			return fmt::vformat(form, values.store());
		}
		catch (const fmt::format_error&)
		{
			return std::nullopt;
		}
	}

	std::optional<Captures> Pattern::match(std::string_view text) const
	{
		Captures captures;
		if (!matchFrom(0, text, 0, captures))
			return std::nullopt;
		return captures;
	}

	bool Pattern::matchFrom(std::size_t index, std::string_view text, std::size_t pos, Captures& captures) const
	{
		const Piece& piece = m_pieces[index];

		if (text.size() - pos < piece.literal.size() || text.compare(pos, piece.literal.size(), piece.literal) != 0)
			return false;
		pos += piece.literal.size();

		if (piece.key.empty())
			return pos == text.size(); // the trailing literal: nothing may be left over

		const Captures::const_iterator already = captures.find(piece.key);
		if (already != captures.end())
		{
			// One name, one capture: a pattern naming a key twice is asserting the two runs are
			// the same text, so a candidate where they differ does not match.
			const std::string& seen = already->second;
			if (text.size() - pos < seen.size() || text.compare(pos, seen.size(), seen) != 0)
				return false;
			return matchFrom(index + 1, text, pos + seen.size(), captures);
		}

		for (std::size_t length = 1; pos + length <= text.size(); ++length)
		{
			captures.emplace(piece.key, std::string{text.substr(pos, length)});
			if (matchFrom(index + 1, text, pos + length, captures))
				return true;
			captures.erase(piece.key);
		}
		return false;
	}
} // namespace lain::string
