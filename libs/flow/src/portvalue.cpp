#include "lain/flow/portvalue.h"

namespace lain::flow
{
	bool PortValue::sameType(const PortValue& other) const
	{
		return type() == other.type();
	}

	void PortValue::clear()
	{
		m_value.reset();
		m_type = std::type_index(typeid(void));
	}
} // namespace lain::flow
