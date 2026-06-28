#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>
#include <lain/app/inputstate.h>
#include <lain/app/timestate.h>
#include <lain/app/window.h>
#include <lain/app/windowdelegate.h>
#include <lain/core/time.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#if defined(__APPLE__)
#	include <cstdlib>
#	include <filesystem>
#	include <mach-o/dyld.h>
#	include <string>
#	include <system_error>
#endif

namespace lain::app
{
	using lain::core::Time;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Direct-launch convenience on macOS: point the loader at the staged MoltenVK
	// ICD unless the caller already set one. A no-op elsewhere.
	static void useStagedVulkanICD()
	{
#if defined(__APPLE__)
		if (std::getenv("VK_ICD_FILENAMES"))
			return;
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string buf(size, '\0');
		if (_NSGetExecutablePath(buf.data(), &size) != 0)
			return;
		std::error_code ec;
		const std::filesystem::path exe = std::filesystem::canonical(buf.c_str(), ec);
		if (ec)
			return;
		const std::filesystem::path icd = exe.parent_path() / "vulkan" / "MoltenVK_icd.json";
		if (std::filesystem::exists(icd))
			setenv("VK_ICD_FILENAMES", icd.string().c_str(), 0);
#endif
	}

	static int glfwKeyCode(Key k)
	{
		switch (k)
		{
			case Key::Space:
				return GLFW_KEY_SPACE;
			case Key::Enter:
				return GLFW_KEY_ENTER;
			case Key::Escape:
				return GLFW_KEY_ESCAPE;
			case Key::Tab:
				return GLFW_KEY_TAB;
			case Key::Backspace:
				return GLFW_KEY_BACKSPACE;
			case Key::Delete:
				return GLFW_KEY_DELETE;
			case Key::Left:
				return GLFW_KEY_LEFT;
			case Key::Right:
				return GLFW_KEY_RIGHT;
			case Key::Up:
				return GLFW_KEY_UP;
			case Key::Down:
				return GLFW_KEY_DOWN;
			case Key::LeftShift:
				return GLFW_KEY_LEFT_SHIFT;
			case Key::RightShift:
				return GLFW_KEY_RIGHT_SHIFT;
			case Key::LeftCtrl:
				return GLFW_KEY_LEFT_CONTROL;
			case Key::RightCtrl:
				return GLFW_KEY_RIGHT_CONTROL;
			case Key::LeftAlt:
				return GLFW_KEY_LEFT_ALT;
			case Key::RightAlt:
				return GLFW_KEY_RIGHT_ALT;
			default:
				break;
		}
		if (k >= Key::A && k <= Key::Z)
			return GLFW_KEY_A + (static_cast<int>(k) - static_cast<int>(Key::A));
		if (k >= Key::Num0 && k <= Key::Num9)
			return GLFW_KEY_0 + (static_cast<int>(k) - static_cast<int>(Key::Num0));
		return GLFW_KEY_UNKNOWN;
	}

	static int glfwButtonCode(MouseButton b)
	{
		switch (b)
		{
			case MouseButton::Left:
				return GLFW_MOUSE_BUTTON_LEFT;
			case MouseButton::Right:
				return GLFW_MOUSE_BUTTON_RIGHT;
			case MouseButton::Middle:
				return GLFW_MOUSE_BUTTON_MIDDLE;
			default:
				return -1;
		}
	}

	// Pick a GPU + graphics queue family; if `present` is a valid surface, require
	// that family to present to it.
	static bool pickDevice(acm::Instance instance, acm::Surface present, uint32_t& gpuIdx, uint32_t& queueIdx)
	{
		const bool needPresent = present.valid();
		for (const acm::GPU& gpu : instance.getAvailableGPUs())
		{
			const acm::GPUSurfaceSupport* sup = nullptr;
			if (needPresent)
			{
				const auto& list = present.getGPUSupport();
				auto it = std::find_if(list.begin(), list.end(),
									   [idx = gpu.index](const acm::GPUSurfaceSupport& s)
									   { return s.gpuIndex == idx; });
				if (it == list.end() || it->supportedFormats.empty() || it->supportedPresentModes.empty())
					continue;
				sup = &*it;
			}
			for (const acm::GPUQueueFamily& qf : gpu.queueFamilies)
			{
				if (!qf.supportsGraphics)
					continue;
				if (needPresent && (qf.index >= sup->queueFamilySupportsPresent.size() || !sup->queueFamilySupportsPresent[qf.index]))
					continue;
				gpuIdx = gpu.index;
				queueIdx = qf.index;
				return true;
			}
		}
		return false;
	}

	static bool pickSurfaceFormat(acm::Surface surface, uint32_t gpuIdx, acm::SurfaceFormat& format, acm::PresentMode& presentMode)
	{
		const auto& list = surface.getGPUSupport();
		auto it = std::find_if(list.begin(), list.end(),
							   [gpuIdx](const acm::GPUSurfaceSupport& s)
							   { return s.gpuIndex == gpuIdx; });
		if (it == list.end() || it->supportedFormats.empty() || it->supportedPresentModes.empty())
			return false;
		format = it->supportedFormats[0];
		presentMode = it->supportedPresentModes[0];
		for (acm::PresentMode pm : it->supportedPresentModes)
			if (pm == acm::PresentMode::Fifo) // vsync — cap to the refresh rate
			{
				presentMode = pm;
				break;
			}
		return true;
	}

	// --- impl -------------------------------------------------------------------

	struct Application::impl
	{
		explicit impl(ApplicationDelegate& d)
			: delegate(d)
		{
		}

		ApplicationDelegate& delegate;
		acm::Instance instance;
		acm::Device device;
		uint32_t gpuIdx{0};
		bool glfwReady{false};
		bool deviceCreated{false};
		bool quit{false};

		struct Entry
		{
			std::unique_ptr<Window> window;
			WindowDelegate* delegate{nullptr};
			acm::Extent2D lastExtent{0, 0};
		};
		std::vector<Entry> windows;

		InputState input;
		lain::math::Vec2f scrollAccum{0.0f, 0.0f};
		bool inputFirst{true};

		void ensureGlfw();
		void ensureInstance();
		acm::Device ensureDevice(acm::Surface present = {});
		void updateInput();
		static void scrollCallback(GLFWwindow* window, double dx, double dy);
	};

	void Application::impl::scrollCallback(GLFWwindow* window, double dx, double dy)
	{
		auto* s = static_cast<impl*>(glfwGetWindowUserPointer(window));
		if (s)
		{
			s->scrollAccum.x += static_cast<float>(dx);
			s->scrollAccum.y += static_cast<float>(dy);
		}
	}

	void Application::impl::ensureGlfw()
	{
		if (glfwReady)
			return;
		glfwInitVulkanLoader(reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr));
		if (glfwInit() != GLFW_TRUE || glfwVulkanSupported() != GLFW_TRUE)
		{
			fprintf(stderr, "lain::app: GLFW init / Vulkan support failed\n");
			return;
		}
		glfwReady = true;
	}

	void Application::impl::ensureInstance()
	{
		if (instance.valid())
			return;
		useStagedVulkanICD();
		instance = acm::Instance("lain", acm::Version{0, 1, 0, 0});
	}

	acm::Device Application::impl::ensureDevice(acm::Surface present)
	{
		if (deviceCreated)
			return device;
		ensureInstance();
		uint32_t queueIdx = 0;
		if (!pickDevice(instance, present, gpuIdx, queueIdx))
		{
			fprintf(stderr, "lain::app: no suitable GPU / queue found\n");
			return {};
		}
		device = instance.createDevice(instance.getAvailableGPUs()[gpuIdx], queueIdx);
		deviceCreated = device.valid();
		return device;
	}

	void Application::impl::updateInput()
	{
		GLFWwindow* w = nullptr;
		for (auto& e : windows)
		{
			GLFWwindow* h = e.window->glfwHandle();
			if (h && glfwGetWindowAttrib(h, GLFW_FOCUSED))
			{
				w = h;
				break;
			}
		}
		if (!w && !windows.empty())
			w = windows.front().window->glfwHandle();
		if (!w)
			return;

		input.prevKeys = input.keys;
		for (int ki = 1; ki < static_cast<int>(Key::Count); ++ki)
		{
			const int code = glfwKeyCode(static_cast<Key>(ki));
			input.keys[static_cast<size_t>(ki)] = code != GLFW_KEY_UNKNOWN && glfwGetKey(w, code) == GLFW_PRESS;
		}
		input.prevButtons = input.buttons;
		for (int bi = 0; bi < static_cast<int>(MouseButton::Count); ++bi)
		{
			const int code = glfwButtonCode(static_cast<MouseButton>(bi));
			input.buttons[static_cast<size_t>(bi)] = code >= 0 && glfwGetMouseButton(w, code) == GLFW_PRESS;
		}

		double cx = 0.0, cy = 0.0;
		glfwGetCursorPos(w, &cx, &cy);
		const lain::math::Vec2f prev = input.cursor;
		input.cursor = {static_cast<float>(cx), static_cast<float>(cy)};
		input.cursorDelta = inputFirst ? lain::math::Vec2f{0.0f, 0.0f} : (input.cursor - prev);
		input.scroll = scrollAccum;
		scrollAccum = {0.0f, 0.0f};
		inputFirst = false;
	}

	// --- Application ------------------------------------------------------------

	Application::Application(ApplicationDelegate& delegate)
		: m(std::make_unique<impl>(delegate))
	{
	}
	Application::~Application() = default;

	Window& Application::createWindow(const WindowSpec& spec, WindowDelegate& delegate)
	{
		impl& s = *m;
		s.ensureGlfw();
		s.ensureInstance();

		std::unique_ptr<Window> window(new Window());
		if (!window->createSurface(s.instance, spec))
			fprintf(stderr, "lain::app: window/surface creation failed for '%s'\n", spec.title.c_str());

		s.ensureDevice(window->surface());

		acm::SurfaceFormat format;
		acm::PresentMode presentMode = acm::PresentMode::Fifo;
		if (!pickSurfaceFormat(window->surface(), s.gpuIdx, format, presentMode) || !window->createSwapChain(s.device, format, presentMode))
			fprintf(stderr, "lain::app: swapchain creation failed for '%s'\n", spec.title.c_str());

		if (GLFWwindow* h = window->glfwHandle())
		{
			glfwSetWindowUserPointer(h, &s);
			glfwSetScrollCallback(h, &impl::scrollCallback);
		}

		delegate.onInit(*window);

		impl::Entry entry;
		entry.window = std::move(window);
		entry.delegate = &delegate;
		entry.lastExtent = entry.window->framebufferExtent();
		s.windows.push_back(std::move(entry));
		return *s.windows.back().window;
	}

	int Application::run(int argc, char** argv)
	{
		impl& s = *m;

		// onInit: let the delegate register CLI options (CLI11's API), then parse argv.
		cli::App cliApp{"lain application"};
		if (!s.delegate.onInit(*this, cliApp))
			return 1;
		try
		{
			cliApp.parse(argc, argv);
		}
		catch (const cli::ParseError& e)
		{
			return cliApp.exit(e);
		}

		if (!s.delegate.onStart(*this))
			return 1;

		if (s.windows.empty())
		{
			process(); // headless: run the work routine once
		}
		else
		{
			const Time loopStart = Time::now();
			Time prevFrame = loopStart;
			uint64_t frame = 0;
			while (!s.quit)
			{
				bool closing = false;
				for (auto& e : s.windows)
					if (e.window->shouldClose())
						closing = true;
				if (closing)
					break;

				const Time nowT = Time::now();
				TimeState time;
				time.elapsed = nowT - loopStart;
				time.delta = nowT - prevFrame;
				time.frame = frame;
				prevFrame = nowT;

				glfwPollEvents();
				s.updateInput();
				for (auto& e : s.windows)
				{
					const acm::Extent2D fb = e.window->framebufferExtent();
					if (fb.width > 0 && fb.height > 0 && (fb.width != e.lastExtent.width || fb.height != e.lastExtent.height))
					{
						e.delegate->onResize(*e.window, fb);
						e.lastExtent = fb;
					}
				}

				s.delegate.onUpdate(*this, time, s.input);
				for (auto& e : s.windows)
					e.delegate->onRender(*e.window, time);

				++frame;
			}
		}

		s.delegate.onStop(*this);

		// Teardown, device-before-surface ordered: release the windows' device objects,
		// destroy the device (flushing deferred destroys while the surfaces live), then
		// the surfaces + GLFW windows, the instance, and GLFW.
		for (auto& e : s.windows)
			e.delegate->onShutdown(*e.window);
		for (auto& e : s.windows)
			e.window->releaseDeviceObjects();
		s.device.reset();
		for (auto& e : s.windows)
			e.window->releaseSurface();
		s.windows.clear();
		s.instance.reset();
		if (s.glfwReady)
		{
			glfwTerminate();
			s.glfwReady = false;
		}

		s.delegate.onShutdown(*this);
		return 0;
	}

	void Application::process() { m->delegate.onProcess(*this); }

	void Application::quit() { m->quit = true; }

	acm::Device Application::device() { return m->ensureDevice(); }

	const InputState& Application::input() const { return m->input; }
} // namespace lain::app
