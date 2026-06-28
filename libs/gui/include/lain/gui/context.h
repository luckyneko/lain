#pragma once

#include <archimedes/acmForward.h>
#include <imgui.h> // ImTextureID

#include <memory>

namespace lain::app
{
	class Application;
	class Window;
} // namespace lain::app

namespace lain::gui
{
	// The ImGui integration seam for one window: owns the ImGui context and the
	// GLFW + Vulkan backends (bound to the window's native handle + the shared device
	// / swapchain render pass). A WindowDelegate composes one — newFrame() at the top
	// of a frame, build UI with lain::gui::, then render() inside the window's
	// renderer record callback:
	//
	//   ctx.newFrame();
	//   lain::gui::Begin("Inspector"); ...; lain::gui::End();
	//   window.renderer().render([&](acm::CommandBuffer cmd, uint32_t) { ctx.render(cmd); });
	//
	// All calls are main-thread only (ImGui is single-threaded).
	class Context
	{
	public:
		Context(lain::app::Application& app, lain::app::Window& window);
		~Context();
		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;

		// Begin a new ImGui frame (platform + renderer new-frame + ImGui::NewFrame).
		void newFrame();

		// Finalize the frame and record its draw data into the command buffer. Call
		// inside acm::Renderer::render's record callback (the render pass is begun).
		void render(acm::CommandBuffer cmd);

		// Register a GPU texture for display, returning an id for lain::gui::Image.
		// The texture must be in SHADER_READ_ONLY layout (an uploaded acm::Texture is).
		ImTextureID image(acm::Texture texture, acm::Sampler sampler);

	private:
		struct impl;
		std::unique_ptr<impl> m;
	};
} // namespace lain::gui
