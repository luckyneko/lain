#pragma once

// Template method definitions for lain::flow::Port (see port.h). They forward to
// the owned PortValue, which stores the value in its type-erased slot.

namespace lain::flow
{
	template <typename T>
	void Port::set(T value)
	{
		m_value.set(std::move(value));
	}

	template <typename T>
	const T& Port::get() const
	{
		return m_value.get<T>();
	}
} // namespace lain::flow
