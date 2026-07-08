#pragma once

// Template method definitions for lain::flow::Param (see param.h). They forward to the
// owned PortValue, exactly as Port's do — the shared value machinery.

namespace lain::flow
{
	template <typename T>
	void Param::set(T value)
	{
		m_value.set(std::move(value));
	}

	template <typename T>
	const T& Param::get() const
	{
		return m_value.get<T>();
	}
} // namespace lain::flow
