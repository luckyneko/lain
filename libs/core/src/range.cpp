#include "lain/core/range.h"

#include <cctype>
#include <limits>

namespace lain::core
{
	std::size_t Range::count() const
	{
		if (step == 0 || last < first)
			return 0;
		return (last - first) / step + 1;
	}

	bool Range::contains(std::size_t value) const
	{
		if (step == 0 || value < first || value > last)
			return false;
		return (value - first) % step == 0;
	}

	std::string Range::toString() const
	{
		std::string text = std::to_string(first);
		if (last != first)
			text += "-" + std::to_string(last);
		if (step != 1)
			text += "x" + std::to_string(step);
		return text;
	}

	// A run of digits at `pos`, advanced past it. nullopt when there is not one, or when the value
	// would not fit — refusing beats wrapping, since a wrapped range visits the wrong values and
	// says nothing about it.
	static std::optional<std::size_t> readNumber(std::string_view text, std::size_t& pos)
	{
		const std::size_t start = pos;
		while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0)
			++pos;
		if (pos == start)
			return std::nullopt;

		std::size_t value = 0;
		for (std::size_t i = start; i < pos; ++i)
		{
			const std::size_t digit = static_cast<std::size_t>(text[i] - '0');
			if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
				return std::nullopt;
			value = value * 10 + digit;
		}
		return value;
	}

	std::optional<Range> Range::parse(std::string_view text)
	{
		std::size_t pos = 0;
		const std::optional<std::size_t> first = readNumber(text, pos);
		if (!first.has_value())
			return std::nullopt;

		Range range;
		range.first = *first;
		range.last = *first; // a bare number is a one-value range, not an error

		if (pos < text.size() && text[pos] == '-')
		{
			++pos;
			const std::optional<std::size_t> last = readNumber(text, pos);
			if (!last.has_value())
				return std::nullopt;
			range.last = *last;
		}

		if (pos < text.size() && text[pos] == 'x')
		{
			++pos;
			const std::optional<std::size_t> step = readNumber(text, pos);
			if (!step.has_value() || *step == 0)
				return std::nullopt;
			range.step = *step;
		}

		if (pos != text.size())
			return std::nullopt; // trailing junk
		if (range.last < range.first)
			return std::nullopt; // reversed: reversing is an operation on a collection, not a range

		return range;
	}

	bool operator==(const Range& a, const Range& b)
	{
		return a.first == b.first && a.last == b.last && a.step == b.step;
	}

	bool operator!=(const Range& a, const Range& b) { return !(a == b); }

	bool lexical_cast(const std::string& input, Range& output)
	{
		const std::optional<Range> parsed = Range::parse(input);
		if (!parsed.has_value())
			return false;
		output = *parsed;
		return true;
	}
} // namespace lain::core
