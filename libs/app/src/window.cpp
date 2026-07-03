#include "lain/app/window.h"

#include <archimedes/archimedes.h>
#include <lain/log/log.h>

#include <cstdint>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace lain::app
{
	struct Window::impl
	{
		Application* app{nullptr};
		WindowSpec spec;
		GLFWwindow* window{nullptr};
		acm::Surface surface;
		acm::SwapChain swapChain;
		acm::Renderer renderer;
	};

	Window::Window(Application& app)
		: m(std::make_unique<impl>())
	{
		m->app = &app;
	}
	Window::~Window() = default;

	Application& Window::app() const { return *m->app; }

	const std::string& Window::title() const { return m->spec.title; }
	acm::Extent2D Window::extent() const { return m->swapChain.getExtents(); }
	bool Window::shouldClose() const { return m->window && glfwWindowShouldClose(m->window); }
	void Window::requestClose()
	{
		if (m->window)
			glfwSetWindowShouldClose(m->window, GLFW_TRUE);
	}

	acm::Renderer Window::renderer() const { return m->renderer; }
	acm::SwapChain Window::swapChain() const { return m->swapChain; }
	void* Window::nativeHandle() const { return m->window; }

	// --- private (Application-driven lifecycle) ---------------------------------

	bool Window::createSurface(acm::Instance instance, const WindowSpec& spec)
	{
		m->spec = spec;
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, spec.resizable ? GLFW_TRUE : GLFW_FALSE);
		m->window = glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);
		if (!m->window)
			return false;
		glfwSetWindowPos(m->window, spec.posX, spec.posY);

		VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(instance.vkInstance(), m->window, nullptr, &vkSurface) != VK_SUCCESS)
		{
			lain::log::error("glfwCreateWindowSurface failed");
			glfwDestroyWindow(m->window);
			m->window = nullptr;
			return false;
		}
		m->surface = instance.createSurface(vkSurface);
		return m->surface.valid();
	}

	acm::Surface Window::surface() const { return m->surface; }

	bool Window::createSwapChain(acm::Device device, acm::SurfaceFormat format, acm::PresentMode presentMode)
	{
		m->swapChain = device.createSwapChain(m->surface, format, presentMode, framebufferExtent(), m->spec.depth, m->spec.samples);
		if (!m->swapChain.valid())
			return false;
		m->renderer = device.createRenderer(m->swapChain);
		return m->renderer.valid();
	}

	GLFWwindow* Window::glfwHandle() const { return m->window; }

	acm::Extent2D Window::framebufferExtent() const
	{
		int w = 0, h = 0;
		glfwGetFramebufferSize(m->window, &w, &h);
		return acm::Extent2D{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
	}

	void Window::releaseDeviceObjects()
	{
		m->renderer.reset();
		m->swapChain.reset();
	}

	void Window::releaseSurface()
	{
		m->surface.reset();
		if (m->window)
		{
			glfwDestroyWindow(m->window);
			m->window = nullptr;
		}
	}
} // namespace lain::app
