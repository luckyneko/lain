#pragma once

#include <lain/math/types.h> // Vec2i (extent)

#include <archimedes/acmDescriptorSet.h> // acm::DescriptorSet (owned — the descriptor ImGui binds)
#include <archimedes/acmTexture.h>		 // acm::Texture (owned)
#include <imgui.h>						 // ImTextureID / ImTextureRef

namespace lain::gui
{
	class Context;

	// An owned, drawable GPU image: a texture uploaded from a lain::image::Image plus the
	// descriptor ImGui binds to draw it. Move-only, so a preview cache is just a map of these.
	// Create via Context::createTexture(); draw with the ordinary gui::Image(tex, size) (a
	// Texture converts implicitly to its ImTextureRef).
	//
	// BOTH HALVES ARE ARCHIMEDES', and that is what makes releasing one safe at any time a frame
	// may still be running. The descriptor is an acm::DescriptorSet rather than one from ImGui's
	// pool (the Context allocates it against a layout identical to ImGui's texture set, so it
	// binds wherever ImGui binds an ImTextureID), and acm defers destroying anything released
	// until every submission already made has completed. An ImGui_ImplVulkan_RemoveTexture, by
	// contrast, freed the set at once, while up to acm::Renderer::MaxFramesInFlight frames that
	// had drawn it could still be executing. Two rules remain, and neither is new:
	//   - release it before the device goes, as with every acm resource (a Device outlives them);
	//   - do not release it between DRAWING it and RENDERING that frame. The frame's draw data
	//     holds its raw handle until Context::render records it, so that is a use after release
	//     however the destroy is deferred.
	//
	// NEVER REWRITTEN IN PLACE. A new picture is a new Texture (Context::createTexture), and the one
	// it replaces is released, which is the only safe way to change what is shown: archimedes'
	// upload starts its transfer from UNDEFINED with no wait on earlier work, so writing into a
	// texture the frames in flight may still be sampling is a write-after-read hazard, and nothing
	// orders it after their reads. Releasing defers the old one past them instead. (There was an
	// upload() that rewrote in place when the size matched; both its callers, flowview's sequence
	// player and its preview cache, had the hazard, so it went rather than gaining a wait.)
	//
	// A Texture holds NO device or Context: allocation (which needs the device) is a
	// Context::createTexture concern. Nor does it need its Context to die: nothing of ImGui's
	// backend is left in it.
	class Texture
	{
	public:
		Texture() = default;
		~Texture() = default; // acm defers destroying both halves; nothing here to reclaim
		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		bool valid() const { return m_id != ImTextureID{}; }
		operator ImTextureRef() const { return m_id; } // draw via gui::Image(tex, size)

		// The uploaded size in pixels ({0, 0} when empty). What a caller fitting this texture into a
		// box needs: the ASPECT belongs to what was uploaded, not to whatever value it was made from
		// — a poster frame decoded out of a frame sequence has no image on the port to ask.
		lain::math::Vec2i extent() const;

	private:
		friend class Context;
		Texture(acm::Texture texture, acm::DescriptorSet descriptor, ImTextureID id);

		acm::Texture m_texture;			 // keeps the uploaded texture alive while it is shown
		acm::DescriptorSet m_descriptor; // the sampled-image descriptor ImGui binds
		ImTextureID m_id{};				 // m_descriptor's raw handle, as ImGui names it
	};
} // namespace lain::gui
