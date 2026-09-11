#include "lain/core/range.h"

#include <cassert>
#include <charconv>

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

	// A run of digits at `pos`, advanced past it. nullopt when there is not one, or when the value
	// would not fit — refusing beats wrapping, since a wrapped range visits the wrong values and
	// says nothing about it.
	//
	// std::from_chars answers both halves at once: it reports result_out_of_range rather than
	// wrapping, and the ptr it hands back IS the advanced position, which is what this incremental
	// form wants. It also stops the classification being locale-dependent, which std::isdigit is.
	static std::optional<std::size_t> readNumber(std::string_view text, std::size_t& pos)
	{
		std::size_t value = 0;
		const std::from_chars_result result = std::from_chars(text.data() + pos, text.data() + text.size(), value);
		if (result.ec != std::errc{})
			return std::nullopt; // no digits here, or a value too large to hold

		pos = static_cast<std::size_t>(result.ptr - text.data());
		return value;
	}

	std::optional<Range> Range::parse(std::string_view text)
	{
		std::size_t pos = 0;
		const std::optional<std::size_t> first = readNumber(text, pos);
		if (!first.has_value())
			return std::nullopt;

		std::size_t last = *first; // a bare number is a one-value range, not an error
		std::size_t step = 1;

		if (pos < text.size() && text[pos] == '-')
		{
			++pos;
			const std::optional<std::size_t> parsed = readNumber(text, pos);
			if (!parsed.has_value())
				return std::nullopt;
			last = *parsed;
		}

		if (pos < text.size() && text[pos] == 'x')
		{
			++pos;
			const std::optional<std::size_t> parsed = readNumber(text, pos);
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

	bool lexical_cast(const std::string& input, Range& output)
	{
		const std::optional<Range> parsed = Range::parse(input);
		if (!parsed.has_value())
			return false;
		output = *parsed;
		return true;
	}
} // namespace lain::core
