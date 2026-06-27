#pragma once

// Template method definitions for lain::flow::PortValue (see portvalue.h). The
// CPU payload lives in the variant's std::any arm.

namespace lain::flow
{
	template <typename T>
	void PortValue::set(T value)
	{
		m_value.emplace<std::any>(std::move(value));
	}

	template <typename T>
	bool PortValue::holds() const
	{
		const std::any* cpu = std::get_if<std::any>(&m_value);
		return cpu != nullptr && cpu->type() == typeid(T);
	}

	template <typename T>
	const T& PortValue::get() const
	{
		return std::any_cast<const T&>(std::get<std::any>(m_value));
	}
}
