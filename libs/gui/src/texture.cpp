#include "lain/gui/texture.h"

#include <lain/image/convert.h> // normalise to RGBA8 (a gui::Texture is always RGBA8)
#include <lain/image/image.h>

#include <utility>

namespace lain::gui
{
	Texture::Texture(acm::Texture texture, acm::DescriptorSet descriptor, ImTextureID id, lain::image::PixelFormat format)
		: m_texture(std::move(texture))
		, m_descriptor(std::move(descriptor))
		, m_id(id)
		, m_format(format)
	{
	}

	// Moves hand both acm halves over and leave the source empty — a moved-from Texture must not
	// go on naming a descriptor it no longer owns. Releasing what this one held is the acm
	// handles' own business, deferred past the frames that drew it.
	Texture::Texture(Texture&& other) noexcept
		: m_texture(std::move(other.m_texture))
		, m_descriptor(std::move(other.m_descriptor))
		, m_id(std::exchange(other.m_id, ImTextureID{}))
		, m_format(other.m_format)
	{
	}

	Texture& Texture::operator=(Texture&& other) noexcept
	{
		if (this != &other)
		{
			m_texture = std::move(other.m_texture);
			m_descriptor = std::move(other.m_descriptor);
			m_id = std::exchange(other.m_id, ImTextureID{});
			m_format = other.m_format;
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

	bool Texture::upload(const lain::image::Image& image)
	{
		if (!valid() || !image.valid())
			return false;
		// A gui::Texture is always RGBA8, so normalise the source the same way createTexture does before
		// reusing in place (else a loaded RGB / Gray file would never match m_format and re-upload).
		// (Fully qualified: the parameter `image` shadows the lain::image namespace.)
		if (image.pixelFormat() != lain::image::PixelFormat::RGBA8)
			return upload(lain::image::convert(image, lain::image::PixelFormat::RGBA8));
		// Reuse only when this texture already matches the image's size + format; otherwise
		// the caller recreates via Context::createTexture().
		if (image.pixelFormat() != m_format)
			return false;
		const acm::Extent2D extent = m_texture.extent();
		if (static_cast<int>(extent.width) != image.width() || static_cast<int>(extent.height) != image.height())
			return false;

		m_texture.upload(image.data(), image.byteSize()); // self-contained: no device
		return true;
	}
} // namespace lain::gui
