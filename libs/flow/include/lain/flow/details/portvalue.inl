#pragma once

// Template method definitions for lain::flow::PortValue (see portvalue.h). The payload lives
// in a shared const allocation, so set() allocates once and holds/get resolve through the
// stored type_index — a shared_ptr<const void> cannot answer "what am I?" on its own.

#include <any> // std::bad_any_cast — deliberately kept as the mismatch exception from when the
			   // slot WAS a std::any, so the documented contract and every caller are unchanged
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace lain::flow
{
	template <typename T>
	void PortValue::set(T value)
	{
		// Moved into a fresh allocation: the slot rebinds, so an earlier copy keeps its payload.
		m_value = std::make_shared<const T>(std::move(value));
		m_type = std::type_index(typeid(T));
	}

	template <typename T>
	bool PortValue::holds() const
	{
		return m_value != nullptr && m_type == std::type_index(typeid(T));
	}

	template <typename T>
	const T& PortValue::get() const
	{
		if (!holds<T>())
			throw std::bad_any_cast();
		// The payload was allocated as a T and m_type just confirmed it, so the round-trip
		// through void is exact.
		return *static_cast<const T*>(m_value.get());
	}
} // namespace lain::flow
