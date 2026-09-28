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
#include <lain/task/task.h>

#include <archimedes/archimedes.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib> // EXIT_SUCCESS / EXIT_FAILURE
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
		int verbosity{0}; // -v/--verbose count, filled in by initialise()'s parse
		int threads{-1};  // --threads, filled in by initialise()'s parse; -1 = hardware_concurrency() - 1
		int exitCode{0};  // what run() returns; a delegate sets it when its own work fails

		// How far the lifecycle got. `Initialised` is reached ONLY by an initialise() that
		// returned nullopt, so an early exit (--version, a bad command line) leaves it at
		// Constructed with nothing brought up — which is what makes shutdown() and the
		// destructor no-ops on those paths, exactly as before the lifecycle was split.
		enum class Phase
		{
			Constructed,
			Initialised,
			ShutDown
		};
		Phase phase{Phase::Constructed};

		// THE CLI OUTLIVES THE PARSE, and must: a delegate keeps the cli::App* its onInit got
		// back from add_subcommand and reads it during onProcess (flowview's `run` / `list` do
		// exactly that, and say so). Held here rather than as a local in initialise(), which is
		// where it started life and where it dangled — the symptom was a HANG in a later phase,
		// with nothing for a compiler to object to. Anything the parser binds a reference to
		// lives here for the same reason.
		std::unique_ptr<cli::App> parser;
		bool showLicenses{false};
		acm::Instance instance;
		acm::Device device;
		bool glfwReady{false};
		bool deviceCreated{false};
		bool quit{false};

		// One window the app owns, its borrowed delegate, and the extent it was last seen at
		// (the resize edge is a comparison against this, so it lives beside the window).
		struct ManagedWindow
		{
			std::unique_ptr<Window> window;
			WindowDelegate* delegate{nullptr};
			acm::Extent2D lastExtent{0, 0};
		};
		std::vector<ManagedWindow> windows;

		InputState input;
		lain::math::Vec2f scrollAccum{0.0f, 0.0f};
		bool inputFirst{true};
	};

	// --- Application ------------------------------------------------------------

	Application::Application(ApplicationDelegate& delegate, AppInfo info)
		: m(std::make_unique<impl>(delegate, std::move(info)))
	{
	}
	// Complain, then do it anyway — multi's WorkerPool contract, one level up and at a point
	// where there is still a stack to say why. The pool must be stopped before static
	// destruction, and this object lives in main, so it is what guarantees the stop runs in
	// time however a caller leaves.
	Application::~Application()
	{
		assert(m->phase != impl::Phase::Initialised && "Application: initialise() without shutdown()");
		shutdown();
	}

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

		impl::ManagedWindow managed;
		managed.window = std::move(window);
		managed.delegate = &delegate;
		managed.lastExtent = managed.window->framebufferExtent();
		s.windows.push_back(std::move(managed));
		return *s.windows.back().window;
	}

	std::optional<int> Application::initialise(int argc, char** argv)
	{
		impl& s = *m;
		assert(s.phase == impl::Phase::Constructed && "Application::initialise: already initialised");

		// onInit: name the CLI after the app, wire up the framework flags (--version,
		// --licenses, -v/--verbose, --threads), let the delegate register its own options
		// (CLI11's API), then parse argv. All four are reserved by the framework; a delegate
		// that re-registers one makes CLI11 throw on construction — caught here and reported,
		// rather than escaping initialise() as an uncaught terminate.
		s.parser = std::make_unique<cli::App>(s.info.name, s.info.name);
		cli::App& cliApp = *s.parser;
		try
		{
			cliApp.set_version_flag("--version", s.info.name + " " + s.info.version.toString());
			cliApp.add_flag("--licenses", s.showLicenses, "print third-party licence notices and exit");
			cliApp.add_flag("-v,--verbose", s.verbosity, "increase log verbosity (-v: debug, -vv: trace)");
			// The process owns ONE pool, so the knob for it belongs to the framework beside the
			// other process-wide ones rather than to each app. 0 is not a degenerate value: it
			// leaves the pool inactive, and every dispatch then runs inline on the caller in
			// dependency order — a deterministic single-threaded execution of a parallel plan,
			// which is the escape hatch to reach for when concurrency is the suspect.
			//
			// LIKE EVERY RESERVED FLAG, it must come BEFORE a subcommand: an app that defines
			// one with allow_extras swallows anything typed after it, so `app run --threads 0`
			// warns about an unmatched argument and silently keeps the default. Pre-existing and
			// shared with -v, hence the help text saying so. CLI11's fallthrough() is the obvious
			// fix and was tried and REVERTED: it makes a subcommand hand unmatched options up to
			// the parent, which then REFUSES them, so flowview's --<boundary> bindings stop
			// working entirely. Measured, not assumed.
			cliApp.add_option("--threads", s.threads, "worker threads, before any subcommand (-1: one per core less this one, 0: run inline)")->capture_default_str();
			if (!s.delegate.onInit(*this, cliApp))
				return EXIT_FAILURE;
		}
		catch (const cli::Error& e)
		{
			lain::log::error("CLI setup failed (did a delegate re-register a reserved flag like --verbose or --version?): {}", e.what());
			return EXIT_FAILURE;
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
		if (s.showLicenses)
		{
			const std::string_view notices = thirdPartyNotices();
			if (notices.empty())
				std::cout << s.info.name << " has no third-party licence notices to report.\n";
			else
				std::cout << notices << std::flush;
			return EXIT_SUCCESS;
		}

		// Raise the log level from -v before anything logs (-v: debug, -vv+: trace).
		if (s.verbosity == 1)
			lain::log::setLevel(lain::log::Level::Debug);
		else if (s.verbosity >= 2)
			lain::log::setLevel(lain::log::Level::Trace);

		// Past the parse (so --version / --help have already printed and exited): a
		// normal run announces its identity.
		lain::log::info("{} {}", s.info.name, s.info.version);

		// The pool starts here and nowhere else: past every exit path above, so --version /
		// --help / --licenses answer without spawning a thread, and before onStart, so a
		// delegate may dispatch from the moment it is brought up. shutdown() stops it after
		// the last onShutdown, so teardown may dispatch too.
		//
		// The start and the stop are now separate public calls rather than a bracket inside one
		// function, so what guarantees the pair is ~Application: it asserts the stop happened and
		// then does it, which is multi's own WorkerPool contract one level up. That object lives
		// in main, so it runs before static destruction — which is the whole requirement. An
		// exception escaping in between needs no cover: it reaches no handler, the process
		// terminates without running static destructors, and the pool is never destroyed to
		// complain about. See docs/adr/0024-one-process-task-pool.md.
		task::start(s.threads);
		s.phase = impl::Phase::Initialised;
		return std::nullopt;
	}

	int Application::run()
	{
		impl& s = *m;
		assert(s.phase == impl::Phase::Initialised && "Application::run: call initialise() first");

		if (!s.delegate.onStart(*this))
			return EXIT_FAILURE;

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

		// What the process reports is whatever the delegate's own work concluded (0 unless it said
		// otherwise). A headless run that stopped early must be able to say so — see exit().
		return s.exitCode;
	}

	void Application::shutdown()
	{
		impl& s = *m;

		// Total and idempotent, so every path can call it: nothing was brought up before
		// initialise() succeeded, and nothing is left after the first call. That is what lets
		// the destructor be a net rather than a second contract to reason about.
		if (s.phase != impl::Phase::Initialised)
			return;
		s.phase = impl::Phase::ShutDown;

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

		// Last, so everything above may still dispatch.
		task::stop();
	}

	int Application::process() { return m->delegate.onProcess(*this); }

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
