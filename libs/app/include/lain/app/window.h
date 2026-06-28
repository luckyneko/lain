#pragma once

#include <archimedes/acmForward.h>
#include <archimedes/acmTypes.h>

#include <memory>
#include <string>

struct GLFWwindow;

namespace lain::app
{
	// One window an Application opens.
	struct WindowSpec
	{
		std::string title{"lain"};
		int width{1280};
		int height{720};
		int posX{120};
		int posY{160};
		bool resizable{true};
		bool depth{true};
		acm::SampleCount samples{acm::SampleCount::One};
	};

	// A window owned by the Application: a GLFW window + acm::Surface + swapchain +
	// renderer, driven by a WindowDelegate. You never construct or destroy one — the
	// Application does (Application::createWindow), handing back a reference valid for
	// its lifetime. pImpl, in lain's archimedes handle style.
	class Window
	{
	public:
		~Window();
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		const std::string& title() const;
		acm::Extent2D extent() const; // current swapchain extent
		bool shouldClose() const;
		void requestClose();

		// The window's renderer — record + present a frame from WindowDelegate::onRender:
		//   window.renderer().render([&](acm::CommandBuffer cmd, uint32_t frame){ ... });
		acm::Renderer renderer() const;
		// The swapchain — for the render pass during pipeline creation, use
		// window.swapChain().vkRenderPass().
		acm::SwapChain swapChain() const;

		// The native window handle (a GLFWwindow*), for a GUI backend that must bind to
		// it (e.g. imgui_impl_glfw). Returned as void* so this header stays GLFW-free;
		// the GUI layer, which already depends on GLFW, casts it back.
		void* nativeHandle() const;

	private:
		friend class Application; // the owning parent-factory builds + drives it

		Window();

		bool createSurface(acm::Instance instance, const WindowSpec& spec);
		acm::Surface surface() const;
		bool createSwapChain(acm::Device device, acm::SurfaceFormat format, acm::PresentMode presentMode);
		GLFWwindow* glfwHandle() const;
		acm::Extent2D framebufferExtent() const;
		void releaseDeviceObjects(); // renderer + swapchain (before device teardown)
		void releaseSurface();		 // surface + GLFW window (after device teardown)

		struct impl;
		std::unique_ptr<impl> m;
	};
} // namespace lain::app
