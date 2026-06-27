#include <lain/flow/portvalue.h>

namespace lain::flow
{
	PortKind PortValue::kind() const
	{
		switch (m_value.index())
		{
			case 0: return PortKind::Empty;
			case 1: return PortKind::Cpu;
			case 2: return PortKind::Texture;
			case 3: return PortKind::Buffer;
			default: return PortKind::Empty; // unreachable: variant is never valueless here
		}
	}

	std::type_index PortValue::type() const
	{
		switch (kind())
		{
			case PortKind::Cpu: return std::get<std::any>(m_value).type();
			case PortKind::Texture: return typeid(acm::Texture);
			case PortKind::Buffer: return typeid(acm::Buffer);
			case PortKind::Empty: break;
		}
		return typeid(void);
	}

	void PortValue::set(acm::Texture texture)
	{
		m_value = std::move(texture);
	}

	void PortValue::set(acm::Buffer buffer)
	{
		m_value = std::move(buffer);
	}

	const acm::Texture& PortValue::texture() const
	{
		return std::get<acm::Texture>(m_value);
	}

	const acm::Buffer& PortValue::buffer() const
	{
		return std::get<acm::Buffer>(m_value);
	}

	bool PortValue::sameType(const PortValue& other) const
	{
		return kind() == other.kind() && type() == other.type();
	}

	void PortValue::clear()
	{
		m_value.emplace<std::monostate>();
	}
}
