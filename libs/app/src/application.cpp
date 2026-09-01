#include "lain/app/application.h"

#include "lain/app/appinfo.h"
#include "lain/app/applicationdelegate.h"
#include "lain/app/cli.h"
#include "lain/app/inputstate.h"
#include "lain/app/notices.h"
#include "lain/app/timestate.h"
#include "lain/app/window.h"
#include "lain/app/windowdelegate.h"

#include <lain/core/time.h>
#include <lain/log/log.h>

#include <archimedes/archimedes.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace lain::app
{
	using lain::core::Time;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Narrow a semantic Version to acm::Version's fields (the Vulkan app version:
	// major/minor are uint8, patch is uint16), clamping each component so a large
	// value saturates rather than wraps.
	static acm::Version toAcmVersion(const lain::core::Version& v)
	{
		const auto clamp8 = [](uint32_t x) -> uint8_t
		{ return static_cast<uint8_t>(std::min<uint32_t>(x, 255)); };
		const auto clamp16 = [](uint32_t x) -> uint16_t
		{ return static_cast<uint16_t>(std::min<uint32_t>(x, 65535)); };
		return acm::Version{clamp8(v.major()), clamp8(v.minor()), clamp16(v.patch())};
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

	// Device + surface selection is now acm's job: Instance::graphicsOptions() reports
	// graphics-capable (deviceIndex, queueFamily) options, and Instance::surfaceOptions
	// (surface) filters those to queues that can present to the surface and attaches the
	// ranked format / present mode / capabilities (SurfacePreferences default to SRGB +
	// FIFO first). ensureDevice() / createWindow() consume front() of those directly.

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
		int verbosity{0}; // -v/--verbose count, filled in by run()'s parse
		int exitCode{0};  // what run() returns; a delegate sets it when its own work fails
		acm::Instance instance;
		acm::Device device;
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

		// The same ranked options acm used to pick the device also carry the swapchain's
		// format / present mode / capabilities; front() is the best per SurfacePreferences.
		const std::vector<acm::SurfaceOption> options = s.instance.surfaceOptions(window->surface());
		if (options.empty() || !window->createSwapChain(s.device, options.front()))
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

		// onInit: name the CLI after the app, wire up the framework flags (--version,
		// --licenses, -v/--verbose), let the delegate register its own options (CLI11's
		// API), then parse argv. All three are reserved by the framework; a delegate that
		// re-registers one makes CLI11 throw on construction — caught here and reported,
		// rather than escaping run() as an uncaught terminate.
		cli::App cliApp{s.info.name, s.info.name};
		bool showLicenses = false;
		try
		{
			cliApp.set_version_flag("--version", s.info.name + " " + s.info.version.toString());
			cliApp.add_flag("--licenses", showLicenses, "print third-party licence notices and exit");
			cliApp.add_flag("-v,--verbose", s.verbosity, "increase log verbosity (-v: debug, -vv: trace)");
			if (!s.delegate.onInit(*this, cliApp))
				return 1;
		}
		catch (const cli::Error& e)
		{
			lain::log::error("CLI setup failed (did a delegate re-register a reserved flag like --verbose or --version?): {}", e.what());
			return 1;
		}

		try
		{
			cliApp.parse(argc, argv);
		}
		catch (const cli::ParseError& e)
		{
			// Also the --help / --version exit path: exit() prints them and returns 0.
			return cliApp.exit(e);
		}

		// --licenses is an exit path like --version, and it goes to STDOUT rather than the
		// log: it is the program's answer, not a diagnostic, so it must survive a redirect
		// and must not be filtered by a log level. A binary with no obliging dependency
		// still answers, rather than printing nothing and looking broken.
		if (showLicenses)
		{
			const std::string_view notices = thirdPartyNotices();
			if (notices.empty())
				std::cout << s.info.name << " has no third-party licence notices to report.\n";
			else
				std::cout << notices << std::flush;
			return 0;
		}

		// Raise the log level from -v before anything logs (-v: debug, -vv+: trace).
		if (s.verbosity == 1)
			lain::log::setLevel(lain::log::Level::Debug);
		else if (s.verbosity >= 2)
			lain::log::setLevel(lain::log::Level::Trace);

		// Past the parse (so --version / --help have already printed and exited): a
		// normal run announces its identity.
		lain::log::info("{} {}", s.info.name, s.info.version.toString());

		if (!s.delegate.onStart(*this))
			return 1;

		if (s.windows.empty())
		{
			// Headless: the work routine runs once and its status IS the process's. A caller who
			// sees only the exit code must be able to tell a complete run from a failed one.
			s.exitCode = process();
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

		// Teardown happens either way; what the process reports is whatever the delegate's own work
		// concluded (0 unless it said otherwise). A headless run that stopped early must be able to
		// say so — see setExitCode.
		return s.exitCode;
	}

	int Application::process() { return m->delegate.onProcess(*this); }

	void Application::quit() { exit(0); }

	acm::Device& Application::device() { return ensureDevice({}); }

	acm::Instance& Application::instance()
	{
		ensureInstance();
		return m->instance;
	}

	ApplicationDelegate& Application::delegate() { return m->delegate; }

	const InputState& Application::input() const { return m->input; }

	const AppInfo& Application::info() const { return m->info; }

	int Application::verbosity() const { return m->verbosity; }

	void Application::exit(int code)
	{
		m->exitCode = code;
		m->quit = true;
	}

	// --- private helpers --------------------------------------------------------

	void Application::ensureGlfw()
	{
		impl& s = *m;
		if (s.glfwReady)
			return;
		// Point the loader at the staged ICD BEFORE glfwVulkanSupported(), which triggers
		// GLFW's one-time Vulkan/surface-extension probe. If the ICD isn't set yet, GLFW
		// caches "no metal surface" and every later glfwCreateWindowSurface fails.
		acm::useStagedVulkanRuntime();
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
		acm::useStagedVulkanRuntime();
		s.instance = acm::Instance(s.info.name.c_str(), toAcmVersion(s.info.version));
		if (!s.instance.valid())
			lain::log::error("Vulkan instance creation failed — no driver/ICD found (macOS: the MoltenVK ICD must be staged next to the binary via acm_stage_vulkan_runtime)");
	}

	acm::Device& Application::ensureDevice(const acm::Surface& present)
	{
		impl& s = *m;
		if (s.deviceCreated)
			return s.device;
		ensureInstance();

		// A window needs a device+queue that can present to its surface; a headless app
		// just needs any graphics queue. acm ranks both, so front() is the pick.
		acm::DeviceOption option;
		if (present.valid())
		{
			const std::vector<acm::SurfaceOption> options = s.instance.surfaceOptions(present);
			if (options.empty())
			{
				lain::log::error("no GPU / queue can present to the window surface");
				return s.device;
			}
			option = options.front().device;
		}
		else
		{
			const std::vector<acm::DeviceOption> options = s.instance.graphicsOptions();
			if (options.empty())
			{
				lain::log::error("no graphics-capable GPU / queue found");
				return s.device;
			}
			option = options.front();
		}

		s.device = s.instance.createDevice(option);
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
