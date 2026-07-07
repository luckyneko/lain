#include "lain/gui/texture.h"

#include <lain/image/image.h>

#include <vulkan/vulkan.h> // VkDescriptorSet — the ImGui descriptor behind an ImTextureID

#include <imgui_impl_vulkan.h>

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

	bool Texture::upload(const lain::image::Image& image)
	{
		// Reuse only when this texture already matches the image's size + format; otherwise
		// the caller recreates via Context::createTexture().
		if (!valid() || !image.valid() || image.pixelFormat() != m_format)
			return false;
		const acm::Extent2D extent = m_texture.extent();
		if (static_cast<int>(extent.width) != image.width() || static_cast<int>(extent.height) != image.height())
			return false;

		m_texture.upload(image.bytes().data(), image.bytes().size()); // self-contained: no device
		return true;
	}
} // namespace lain::gui
