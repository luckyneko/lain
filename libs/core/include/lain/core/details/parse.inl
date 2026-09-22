#pragma once

// The bodies of core::parse / core::parseAt / core::parseInto. Split out of parse.h for the reason details/uri.inl is
// split out of uri.h: the header should read as the interface, and this is implementation.

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string>
#include <system_error>
#include <type_traits>

namespace lain::core
{
	template <typename T>
	std::optional<T> parseAt(std::string_view text, std::size_t& pos)
	{
		// bool is excluded because it is not a number here: std::from_chars has no overload for one,
		// and "true" is a word rather than a numeral, so whoever reads one is deciding a spelling
		// (flowview's cli binder takes "true" / "1") rather than parsing.
		static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>,
					  "core::parse reads a number: any integral type, or float / double / long double");

		if (pos > text.size())
			return std::nullopt; // text.data() + pos would be past the end

		const std::string_view rest = text.substr(pos);

		// std::strtof is looser than std::from_chars in three spellings — a leading '+', leading
		// whitespace, and a hex float ("0x1p3" is 8). They are refused here, ahead of BOTH arms, so
		// which mechanism ran is never visible in the answer; a test pins that, since nothing about
		// two separate mechanisms makes it true on its own. The whitespace set is spelled out rather
		// than asked of std::isspace, which is locale-dependent — the same reason string/pattern.cpp
		// spells out its own character classes.
		if (!rest.empty())
		{
			const char first = rest.front();
			const std::string_view afterSign = (first == '-') ? rest.substr(1) : rest;

			if (first == '+')
				return std::nullopt;
			if (first == ' ' || first == '\t' || first == '\n' || first == '\v' || first == '\f' || first == '\r')
				return std::nullopt;
			if (afterSign.size() >= 2 && afterSign[0] == '0' && (afterSign[1] == 'x' || afterSign[1] == 'X'))
				return std::nullopt;
		}

		T value = 0;
		std::size_t read = 0;

		if constexpr (std::is_floating_point_v<T>)
		{
			// NOT std::from_chars — parse.h carries the reason. std::strtof also wants a null
			// terminator, which a string_view has no way to offer, so the field is copied; for any
			// real number that lands inside std::string's small buffer.
			const std::string field(rest);
			char* end = nullptr;
			errno = 0;

			if constexpr (std::is_same_v<T, float>)
				value = std::strtof(field.c_str(), &end);
			else if constexpr (std::is_same_v<T, double>)
				value = std::strtod(field.c_str(), &end);
			else
				value = std::strtold(field.c_str(), &end);

			if (end == field.c_str())
				return std::nullopt; // read nothing — not a number here, and an empty field lands here
			if (errno == ERANGE)
				return std::nullopt; // out of range: refusing beats handing back HUGE_VALF

			read = static_cast<std::size_t>(end - field.c_str());
		}
		else
		{
			const std::from_chars_result result = std::from_chars(rest.data(), rest.data() + rest.size(), value);
			if (result.ec != std::errc{})
				return std::nullopt; // no digits here, or a value too large to hold

			read = static_cast<std::size_t>(result.ptr - rest.data());
		}

		pos += read;
		return value;
	}

	template <typename T>
	std::optional<T> parse(std::string_view text)
	{
		std::size_t pos = 0;
		const std::optional<T> value = parseAt<T>(text, pos);
		if (!value.has_value() || pos != text.size())
			return std::nullopt;
		return value;
	}

	template <typename T>
	bool parseInto(std::string_view text, T& output)
	{
		const std::optional<T> parsed = T::parse(text);
		if (!parsed.has_value())
			return false; // untouched on a refusal — see parse.h
		output = *parsed;
		return true;
	}
} // namespace lain::core
