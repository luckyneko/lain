#pragma once

#include <lain/gui/texture.h> // gui::Texture (returned by upload)

#include <archimedes/acmForward.h>

#include <memory>
#include <string>

namespace lain::app
{
	class Application;
	class Window;
} // namespace lain::app

namespace lain::image
{
	class Image;
}

namespace lain::gui
{
	// The ImGui integration seam for one window: owns the ImGui context and the
	// GLFW + Vulkan backends (bound to the window's native handle + the shared device /
	// swapchain, via acm::interop + dynamic rendering). A WindowDelegate composes one —
	// newFrame() at the top
	// of a frame, build UI with lain::gui::, then render() inside the window's
	// renderer record callback:
	//
	//   ctx.newFrame();
	//   lain::gui::Begin("Inspector"); ...; lain::gui::End();
	//   window.renderer().render([&](acm::CommandBuffer cmd, uint32_t) { ctx.render(cmd); });
	//
	// Construction options — a struct so each is named at the call site (rather than a bare trailing
	// bool with no meaning on its own).
	struct ContextConfig
	{
		// The ImGui layout-persistence file — loaded on construction and saved (throttled) while
		// running, so panel positions/sizes survive a restart. Empty (the default) disables persistence
		// entirely: nothing is read or written, so no stray imgui.ini appears. A multi-window app gives
		// each window a distinct name. ImGui holds the pointer (it does not copy), so the Context keeps
		// the string alive for its lifetime.
		std::string iniFilename;
		// Opt in to ImGui docking (ConfigFlags_DockingEnable) — the host then hosts a DockSpace and
		// windows tile with splitters. Off by default so a fixed-layout gui client isn't given docking
		// behaviour it didn't ask for. (Multi-viewport stays off regardless.)
		bool docking = false;
	};

	// All calls are main-thread only (ImGui is single-threaded).
	class Context
	{
	public:
		Context(lain::app::Application& app, lain::app::Window& window, ContextConfig config = {});
		~Context();
		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;

		// Begin a new ImGui frame (platform + renderer new-frame + ImGui::NewFrame).
		void newFrame();

		// Finalize the frame and record its draw data into the command buffer. Call
		// inside acm::Renderer::render's record callback (the render pass is begun).
		void render(acm::CommandBuffer cmd);

		// Allocate a GPU texture sized to `image` (via this Context's shared device), upload
		// its pixels, and register it for display — the app-facing preview path, with no
		// VkImageView or ImTextureID leaking out. Returns an invalid handle for an empty or
		// unsupported-format image. To refresh an existing texture in place, prefer
		// gui::Texture::upload(); recreate here only on first sight or a resize.
		Texture createTexture(const lain::image::Image& image);

	private:
		struct impl;
		std::unique_ptr<impl> m;
	};
} // namespace lain::gui
