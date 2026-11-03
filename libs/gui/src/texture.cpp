#include "lain/gui/texture.h"

#include <utility>

namespace lain::gui
{
	Texture::Texture(acm::Texture texture, acm::DescriptorSet descriptor, ImTextureID id)
		: m_texture(std::move(texture))
		, m_descriptor(std::move(descriptor))
		, m_id(id)
	{
	}

	// Moves hand both acm halves over and leave the source empty — a moved-from Texture must not
	// go on naming a descriptor it no longer owns. Releasing what this one held is the acm
	// handles' own business, deferred past the frames that drew it.
	Texture::Texture(Texture&& other) noexcept
		: m_texture(std::move(other.m_texture))
		, m_descriptor(std::move(other.m_descriptor))
		, m_id(std::exchange(other.m_id, ImTextureID{}))
	{
	}

	Texture& Texture::operator=(Texture&& other) noexcept
	{
		if (this != &other)
		{
			m_texture = std::move(other.m_texture);
			m_descriptor = std::move(other.m_descriptor);
			m_id = std::exchange(other.m_id, ImTextureID{});
		}
		return *this;
	}

	lain::math::Vec2i Texture::extent() const
	{
		if (!valid())
			return lain::math::Vec2i{0, 0};
		const acm::Extent2D extent = m_texture.extent();
		return lain::math::Vec2i{static_cast<int>(extent.width), static_cast<int>(extent.height)};
	}
} // namespace lain::gui
