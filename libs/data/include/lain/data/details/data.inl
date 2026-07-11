#pragma once

// Bodies of the toValue / fromValue facade (see data.h) — thin public wrappers over the dispatch
// engine (details/reflect.h). The recursion lives in the engine; these are just the entry points.

#include "lain/data/details/reflect.h"

namespace lain::data
{
	template <typename T>
	Value toValue(const T& value)
	{
		return detail::writeValue(value);
	}

	template <typename T>
	std::optional<T> fromValue(const Value& value)
	{
		T out{};
		if (detail::readValue(value, out))
			return out;
		return std::nullopt;
	}
} // namespace lain::data
