#include <lain/flow/portvalue.h>

namespace lain::flow
{
	std::type_index PortValue::type() const
	{
		return m_value.has_value() ? std::type_index(m_value.type()) : std::type_index(typeid(void));
	}

	bool PortValue::sameType(const PortValue& other) const
	{
		return type() == other.type();
	}

	void PortValue::clear()
	{
		m_value.reset();
	}
} // namespace lain::flow
