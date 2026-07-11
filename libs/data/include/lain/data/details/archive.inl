#pragma once

// Body of Archive::member (see archive.h) — the per-field binder, both directions. It bottoms
// out on the dispatch engine (detail::writeValue / detail::readValue), never on the public facade.

#include "lain/data/details/reflect.h"

#include <string>
#include <utility>

namespace lain::data
{
	template <typename T>
	Archive& Archive::member(std::string_view key, T& value)
	{
		if constexpr (detail::is_optional_v<T>)
		{
			if (saving())
			{
				if (value) // an empty optional omits the key entirely
					m_out->set(std::string(key), detail::writeValue(*value));
			}
			else if (const Value* child = m_in->find(key))
			{
				typename T::value_type tmp{};
				if (detail::readValue(*child, tmp))
					value = std::move(tmp);
				else
					value.reset();
			}
			else
			{
				value.reset();
			}
		}
		else
		{
			if (saving())
				m_out->set(std::string(key), detail::writeValue(value));
			else if (const Value* child = m_in->find(key))
				detail::readValue(*child, value);
			// absent key on load: leave value at its default (tolerant)
		}
		return *this;
	}
} // namespace lain::data
