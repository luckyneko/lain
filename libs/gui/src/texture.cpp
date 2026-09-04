#include "lain/gui/texture.h"

#include <lain/image/convert.h> // normalise to RGBA8 (a gui::Texture is always RGBA8)
#include <lain/image/image.h>

#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h> // VkDescriptorSet — the ImGui descriptor behind an ImTextureID

#include <utility>

namespace lain::gui
{
	Texture::Texture(acm::Texture texture, ImTextureID id, lain::image::PixelFormat format)
		: m_texture(std::move(texture))
		, m_id(id)
		, m_format(format)
	{
	}

	Texture::Texture(Texture&& other) noexcept
		: m_texture(std::move(other.m_texture))
		, m_id(other.m_id)
		, m_format(other.m_format)
	{
		other.m_id = ImTextureID{};
	}

	Texture& Texture::operator=(Texture&& other) noexcept
	{
		if (this != &other)
		{
			if (m_id != ImTextureID{})
				ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_id));
			m_texture = std::move(other.m_texture);
			m_id = other.m_id;
			m_format = other.m_format;
			other.m_id = ImTextureID{};
		}
		return *this;
	}

	Texture::~Texture()
	{
		if (m_id != ImTextureID{})
			ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_id));
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
