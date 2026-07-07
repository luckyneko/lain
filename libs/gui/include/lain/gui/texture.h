#pragma once

#include <archimedes/acmTexture.h> // acm::Texture (owned)
#include <imgui.h>				   // ImTextureID / ImTextureRef

namespace lain::image
{
	enum class PixelFormat;
	class Image;
} // namespace lain::image

namespace lain::gui
{
	class Context;

	// An owned, drawable GPU image: a texture uploaded from a lain::image::Image plus its
	// registered ImGui descriptor. Move-only; reclaims the descriptor (and the texture)
	// when it dies, so a preview cache is just a map of these. Create via
	// Context::createTexture(); draw with the ordinary gui::Image(tex, size) (a Texture
	// converts implicitly to its ImTextureRef). Must be released before its Context is
	// destroyed — the descriptor lives in the Context's ImGui backend.
	//
	// A Texture holds NO device or Context: allocation (which needs the device) is a
	// Context::createTexture concern, while upload() below re-uploads pixels in place —
	// acm::Texture::upload is self-contained, so it needs nothing external.
	class Texture
	{
	public:
		Texture() = default;
		~Texture();
		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		bool valid() const { return m_id != ImTextureID{}; }
		operator ImTextureRef() const { return m_id; } // draw via gui::Image(tex, size)

		// Re-upload pixels into the existing texture, in place, when `image` matches this
		// texture's current extent + format — reusing the texture and its descriptor (no
		// reallocation, no re-register). Returns false when this texture is empty or the
		// image's extent/format differs; the caller then recreates via
		// Context::createTexture(). Needs no device (acm::Texture::upload is self-contained).
		bool upload(const lain::image::Image& image);

	private:
		friend class Context;
		Texture(acm::Texture texture, ImTextureID id, lain::image::PixelFormat format);

		acm::Texture m_texture;				 // keeps the uploaded texture alive while it is shown
		ImTextureID m_id{};					 // the ImGui descriptor
		lain::image::PixelFormat m_format{}; // source format, for the in-place-reuse check
	};
} // namespace lain::gui
