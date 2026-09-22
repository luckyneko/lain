#include "lain/core/range.h"

#include "lain/core/parse.h"

#include <cassert>

namespace lain::core
{
	Range::Range(std::size_t first, std::size_t last, std::size_t step)
		: m_first(first)
		, m_last(last < first ? first : last)
		, m_step(step == 0 ? 1 : step)
	{
		assert(last >= first && "core::Range: last precedes first — parse() is the door for untrusted text");
		assert(step >= 1 && "core::Range: a step of 0 would never terminate");
	}

	std::string Range::toString() const
	{
		std::string text = std::to_string(m_first);
		if (m_last != m_first)
			text += "-" + std::to_string(m_last);
		if (m_step != 1)
			text += "x" + std::to_string(m_step);
		return text;
	}

	std::optional<Range> Range::parse(std::string_view text)
	{
		// core::parseAt rather than core::parse: a range is SCANNED, so each number is a leading run
		// with the position advanced past it, and what follows is a separator this reads next.
		// Refusing a value too large to hold is the part that matters here — a wrapped range visits
		// the wrong values and says nothing about it.
		std::size_t pos = 0;
		const std::optional<std::size_t> first = core::parseAt<std::size_t>(text, pos);
		if (!first.has_value())
			return std::nullopt;

		std::size_t last = *first; // a bare number is a one-value range, not an error
		std::size_t step = 1;

		if (pos < text.size() && text[pos] == '-')
		{
			++pos;
			const std::optional<std::size_t> parsed = core::parseAt<std::size_t>(text, pos);
			if (!parsed.has_value())
				return std::nullopt;
			last = *parsed;
		}

		if (pos < text.size() && text[pos] == 'x')
		{
			++pos;
			const std::optional<std::size_t> parsed = core::parseAt<std::size_t>(text, pos);
			if (!parsed.has_value() || *parsed == 0)
				return std::nullopt;
			step = *parsed;
		}

		if (pos != text.size())
			return std::nullopt; // trailing junk
		if (last < *first)
			return std::nullopt; // reversed: reversing is an operation on a collection, not a range

		// Every refusal is above, so the constructor's precondition holds by construction here.
		return Range{*first, last, step};
	}
} // namespace lain::core
