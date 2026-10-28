#pragma once

#include <archimedes/acmTexture.h> // acm::Texture (what a provider answers)
#include <imgui.h>				   // ImTextureID / ImTextureRef / ImDrawData

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace lain::gui
{
	// What a frame-resolved texture is drawn from: the texture for one frame in flight, by the index
	// acm::Renderer::render hands that frame's callbacks (below acm::Renderer::MaxFramesInFlight). It
	// answers a texture a fragment shader can sample, in shader-read layout by the time the frame's
	// draw pass runs (a render::Viewport's is, once its record() has run in that frame's pre-pass), or
	// an invalid texture for "nothing to show", which draws transparent.
	using FrameTextureProvider = std::function<acm::Texture(std::uint32_t frame)>;

	namespace detail
	{
		struct FrameTextureSource; // the provider, held by the handle and watched by the Context
	} // namespace detail

	// A texture a pane shows but did not upload, which can differ from one frame in flight to the
	// next: a render::Viewport's picture is drawn into a texture per frame, and only once the frame's
	// pre-pass records it, after the UI was built. So what a pane draws is a STAND-IN, and
	// Context::render(cmd, frameIndex) asks the provider for that frame's texture and puts its
	// descriptor where the stand-in was, just before ImGui records the frame. A texture replaced in
	// between (a view resized, so its target was made again) is the one drawn, never the one there was
	// when the UI was built.
	//
	// Move-only. Made by Context::frameTexture(); drawn with gui::Image(frameTexture, size). It holds
	// no Context, as a gui::Texture holds none: the Context watches the handle, a stand-in whose
	// handle has died draws transparent, and a handle that outlives its Context is harmless.
	class FrameTexture
	{
	public:
		FrameTexture() = default;
		FrameTexture(FrameTexture&&) noexcept = default;
		FrameTexture& operator=(FrameTexture&&) noexcept = default;
		FrameTexture(const FrameTexture&) = delete;
		FrameTexture& operator=(const FrameTexture&) = delete;
		~FrameTexture() = default;

		bool valid() const { return m_source != nullptr; }

		// The stand-in, for gui::Image: the address of this handle's own source, so it is unique while
		// the handle lives, and names nothing a driver made. Invalid for an empty handle.
		operator ImTextureRef() const;

	private:
		friend class Context;
		explicit FrameTexture(std::shared_ptr<detail::FrameTextureSource> source);
		std::shared_ptr<detail::FrameTextureSource> m_source;
	};

	namespace detail
	{
		// The texture id a source's stand-in carries: its address.
		ImTextureID standInOf(const FrameTextureSource* source);

		// Puts resolve(id) in place of every draw command's texture id that `resolve` answers (a
		// stand-in), and leaves the rest alone: ImGui's own font atlas, a gui::Texture, an id `resolve`
		// does not know. A command that carries a callback, or names a texture ImGui manages
		// (ImTextureData), is never offered. Returns how many commands it changed. Context::render runs
		// it on each frame's draw data; it is here so that the rule can be tested with no device.
		std::size_t resolveStandIns(ImDrawData& data, const std::function<std::optional<ImTextureID>(ImTextureID)>& resolve);
	} // namespace detail
} // namespace lain::gui
