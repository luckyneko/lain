#include "lain/flow/portvalue.h"

namespace lain::flow
{
	bool PortValue::sameType(const PortValue& other) const
	{
		return type() == other.type();
	}

	bool PortValue::samePayload(const PortValue& other) const
	{
		// The STORED pointer, not the control block: every alias of one collection shares its owner's
		// control block, so comparing those would call two different elements one payload.
		return m_value.get() == other.m_value.get() && m_type == other.m_type;
	}

	void PortValue::clear()
	{
		m_value.reset();
		m_type = std::type_index(typeid(void));
	}
} // namespace lain::flow
