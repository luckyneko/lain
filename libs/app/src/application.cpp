#include <archimedes/archimedes.h>
#include <lain/app/appinfo.h>
#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>
#include <lain/app/inputstate.h>
#include <lain/app/timestate.h>
#include <lain/app/window.h>
#include <lain/app/windowdelegate.h>
#include <lain/core/time.h>
#include <lain/log/log.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
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

	// Narrow a semantic Version to acm::Version's uint8 fields (the Vulkan app
	// version), clamping each component so a large value saturates rather than wraps.
	static acm::Version toAcmVersion(const lain::core::Version& v)
	{
		const auto clamp = [](uint32_t x) -> uint8_t
		{ return static_cast<uint8_t>(std::min<uint32_t>(x, 255)); };
		return acm::Version{clamp(v.major()), clamp(v.minor()), clamp(v.patch()), 0};
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

	// --- impl (data only) -------------------------------------------------------

	struct Application::impl
	{
		impl(ApplicationDelegate& d, AppInfo i)
			: delegate(d)
			, info(std::move(i))
		{
		}

		ApplicationDelegate& delegate;
		AppInfo info;
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
	};

	// --- Application ------------------------------------------------------------

	Application::Application(ApplicationDelegate& delegate, AppInfo info)
		: m(std::make_unique<impl>(delegate, std::move(info)))
	{
	}
	Application::~Application() = default;

	Window& Application::createWindow(const WindowSpec& spec, WindowDelegate& delegate)
	{
		impl& s = *m;
		ensureGlfw();
		ensureInstance();

		std::unique_ptr<Window> window(new Window(*this));
		if (!window->createSurface(s.instance, spec))
			lain::log::error("window/surface creation failed for '{}'", spec.title);

		ensureDevice(window->surface());

		acm::SurfaceFormat format;
		acm::PresentMode presentMode = acm::PresentMode::Fifo;
		if (!pickSurfaceFormat(window->surface(), s.gpuIdx, format, presentMode) || !window->createSwapChain(s.device, format, presentMode))
			lain::log::error("swapchain creation failed for '{}'", spec.title);

		if (GLFWwindow* h = window->glfwHandle())
		{
			// The scroll callback needs only the accumulator; point the user pointer at it.
			glfwSetWindowUserPointer(h, &s.scrollAccum);
			glfwSetScrollCallback(h, [](GLFWwindow* sw, double dx, double dy)
								  {
				if (auto* accum = static_cast<lain::math::Vec2f*>(glfwGetWindowUserPointer(sw)))
				{
					accum->x += static_cast<float>(dx);
					accum->y += static_cast<float>(dy);
				} });
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

		// onInit: name the CLI after the app, wire up --version, let the delegate
		// register its own options (CLI11's API), then parse argv.
		cli::App cliApp{s.info.name, s.info.name};
		cliApp.set_version_flag("--version", s.info.name + " " + s.info.version.toString());
		if (!s.delegate.onInit(*this, cliApp))
			return 1;
		try
		{
			cliApp.parse(argc, argv);
		}
		catch (const cli::ParseError& e)
		{
			// Also the --help / --version exit path: exit() prints them and returns 0.
			return cliApp.exit(e);
		}

		// Past the parse (so --version / --help have already printed and exited): a
		// normal run announces its identity.
		lain::log::info("{} {}", s.info.name, s.info.version.toString());

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
				updateInput();
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

	acm::Device Application::device() { return ensureDevice({}); }

	acm::Instance Application::instance()
	{
		ensureInstance();
		return m->instance;
	}

	ApplicationDelegate& Application::delegate() { return m->delegate; }

	const InputState& Application::input() const { return m->input; }

	const AppInfo& Application::info() const { return m->info; }

	// --- private helpers --------------------------------------------------------

	void Application::ensureGlfw()
	{
		impl& s = *m;
		if (s.glfwReady)
			return;
		glfwInitVulkanLoader(reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr));
		if (glfwInit() != GLFW_TRUE || glfwVulkanSupported() != GLFW_TRUE)
		{
			lain::log::error("GLFW init / Vulkan support failed");
			return;
		}
		s.glfwReady = true;
	}

	void Application::ensureInstance()
	{
		impl& s = *m;
		if (s.instance.valid())
			return;
		useStagedVulkanICD();
		s.instance = acm::Instance(s.info.name.c_str(), toAcmVersion(s.info.version));
	}

	acm::Device Application::ensureDevice(acm::Surface present)
	{
		impl& s = *m;
		if (s.deviceCreated)
			return s.device;
		ensureInstance();
		uint32_t queueIdx = 0;
		if (!pickDevice(s.instance, present, s.gpuIdx, queueIdx))
		{
			lain::log::error("no suitable GPU / queue found");
			return {};
		}
		s.device = s.instance.createDevice(s.instance.getAvailableGPUs()[s.gpuIdx], queueIdx);
		s.deviceCreated = s.device.valid();
		return s.device;
	}

	void Application::updateInput()
	{
		impl& s = *m;
		GLFWwindow* w = nullptr;
		for (auto& e : s.windows)
		{
			GLFWwindow* h = e.window->glfwHandle();
			if (h && glfwGetWindowAttrib(h, GLFW_FOCUSED))
			{
				w = h;
				break;
			}
		}
		if (!w && !s.windows.empty())
			w = s.windows.front().window->glfwHandle();
		if (!w)
			return;

		InputState& input = s.input;
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
		input.cursorDelta = s.inputFirst ? lain::math::Vec2f{0.0f, 0.0f} : (input.cursor - prev);
		input.scroll = s.scrollAccum;
		s.scrollAccum = {0.0f, 0.0f};
		s.inputFirst = false;
	}
} // namespace lain::app
