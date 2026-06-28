#pragma once

// Template method definitions for lain::flow::PortValue (see portvalue.h). The
// payload lives in a std::any, so set/holds/get are thin forwards to it.

namespace lain::flow
{
	template <typename T>
	void PortValue::set(T value)
	{
		m_value = std::move(value);
	}

	template <typename T>
	bool PortValue::holds() const
	{
		return m_value.type() == typeid(T);
	}

	template <typename T>
	const T& PortValue::get() const
	{
		return std::any_cast<const T&>(m_value);
	}
}
