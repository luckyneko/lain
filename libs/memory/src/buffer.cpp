#include "lain/memory/buffer.h"

#include <utility>

namespace lain::memory
{
	Buffer::Buffer(std::size_t size, std::size_t align)
		: m_size(size)
		, m_align(align)
	{
		// A zero-size buffer stays empty — no allocation, data() == nullptr.
		if (size > 0)
			m_data = static_cast<std::byte*>(alloc(size, align));
	}

	Buffer::~Buffer()
	{
		reset();
	}

	Buffer::Buffer(Buffer&& other) noexcept
		: m_data(other.m_data)
		, m_size(other.m_size)
		, m_align(other.m_align)
	{
		other.m_data = nullptr;
		other.m_size = 0;
	}

	Buffer& Buffer::operator=(Buffer&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			m_data = other.m_data;
			m_size = other.m_size;
			m_align = other.m_align;
			other.m_data = nullptr;
			other.m_size = 0;
		}
		return *this;
	}

	std::string Buffer::toString() const
	{
		return "Buffer " + std::to_string(m_size) + " bytes (align " + std::to_string(m_align) + ")";
	}

	void Buffer::reset() noexcept
	{
		if (m_data != nullptr)
			dealloc(m_data, m_size, m_align);
		m_data = nullptr;
		m_size = 0;
	}
} // namespace lain::memory
