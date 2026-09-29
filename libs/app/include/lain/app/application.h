#pragma once

#include "lain/app/appinfo.h" // AppInfo (name + version identity)
#include "lain/app/window.h"  // WindowSpec + Window (createWindow return)

#include <archimedes/acmForward.h>

#include <cassert>
#include <cstdlib> // EXIT_SUCCESS (the default exit status)
#include <memory>
#include <optional>

namespace lain::app
{
	class ApplicationDelegate;
	class WindowDelegate;
	struct InputState;

	// Owns the Vulkan instance, the one shared acm::Device, the windows it creates,
	// and the run loop. Driven by an ApplicationDelegate; each window by its own
	// WindowDelegate. Sole owner (non-copyable); delegates are borrowed — you keep
	// them alive past the Application, which its destructor relies on.
	//
	// THE LIFECYCLE IS THE INTERFACE: initialise -> run -> shutdown, in that order, once
	// each. A main is four lines:
	//
	//     Application app(delegate, {"myapp", {0, 1, 0}});
	//     if (const auto code = app.initialise(argc, argv))
	//         return *code;
	//     const int status = app.run();
	//     app.shutdown();
	//     return status;
	//
	// Deliberately NOT hidden behind a macro that writes main for you. It would save three
	// lines in the one app this tree has, while hiding where the program starts and breaking
	// a grep for "int main" — and it is under-determined by a single caller: how the delegate
	// is constructed, where the AppInfo comes from, whether it can be opted out of. Trigger
	// for revisiting: a second app.
	class Application
	{
	public:
		// `info` names the app (CLI program name + help, the Vulkan instance app name,
		// the --version string, and the startup log line) and its version.
		Application(ApplicationDelegate& delegate, AppInfo info);
		~Application();
		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		// Create an App-owned window with its delegate. Call from onStart. The shared
		// device is created on the first call (so device() is valid afterwards), and
		// later windows must be present-compatible with it. Returns a reference valid
		// for the Application's lifetime.
		Window& createWindow(const WindowSpec& spec, WindowDelegate& delegate);

		// Phase 1. onInit(cli) -> parse argv -> apply -v -> log the banner -> start the process
		// task pool (see --threads). Nothing is brought up before this and nothing after it can
		// be skipped, so it is the only phase that reads the command line.
		//
		// A VALUE means stop here and exit with it — --version / --help / --licenses (0), a bad
		// command line, or onInit returning false (non-zero). Nullopt means carry on. Three
		// outcomes, which is why this is not an int: a bare 0 cannot separate "carry on" from
		// "we printed the version". An exit leaves nothing started, so shutdown() has nothing
		// to do and the destructor stays quiet.
		[[nodiscard]] std::optional<int> initialise(int argc, char** argv);

		// Phase 2. onStart -> then:
		//   gui-mode (windows opened): the frame loop (onUpdate + render each window)
		//     until every window closes or exit() is called;
		//   headless (no windows): onProcess once.
		// -> onStop. Returns the exit code the process should report.
		//
		// onStart returning false reports EXIT_FAILURE and skips onStop — it never started.
		// Teardown still happens, in shutdown(), because onInit already ran and the delegate
		// may be holding what it allocated there.
		int run();

		// Phase 3. Drain the GPU, then window and device teardown (device before surface),
		// onShutdown, then stop the process task pool. Idempotent, and a no-op on an Application
		// that never got past initialise — so calling it on any path is safe.
		//
		// The drain comes FIRST, before any WindowDelegate::onShutdown: the frame loop ends with
		// its last frames still executing, and a delegate releases what they read there.
		//
		// The destructor calls it if you did not, after asserting in debug that you did: the
		// pool must be stopped before static destruction, and this object is what guarantees
		// it runs in time. See docs/adr/0024-one-process-task-pool.md.
		void shutdown();

		// Invoke the delegate's onProcess (the work routine) and return its status — 0 for success.
		// run() calls this once in headless-mode and reports the status as the process exit code;
		// in gui-mode a delegate calls it on demand (e.g. a "Run" button) and can act on the result.
		int process();

		// End the run with `code`, taking effect after the current gui loop iteration; in
		// headless mode it sets the status that run() returns.
		//
		// The gui counterpart of onProcess's return value: a windowed app has no single work
		// routine whose status could stand for the run, so a delegate that concludes it has
		// failed says so here. Defaulted, so the common "just close cleanly" case reads
		// app.exit() and there is ONE verb for ending a run rather than two.
		//
		// Known gap, pre-existing: initialise()'s exits — onInit returning false, a CLI parse
		// error, --version, --licenses — answer with their own code and never reach this, so a
		// status set from onInit is discarded. Confined to that one phase now, where before it
		// was spread through run(); still a gap, because those paths have no run to report on.
		void exit(int code = EXIT_SUCCESS);

		// The shared device, created on demand (for windows or headless compute).
		// Returned by reference: acm::Device is a move-only owning root, so the app
		// holds the one instance and hands out access to it.
		acm::Device& device();

		// The Vulkan instance (created on demand) — for a GUI backend that needs the
		// raw VkInstance (e.g. ImGui's Vulkan init). By reference for the same reason
		// as device(): acm::Instance is a move-only owning root.
		acm::Instance& instance();

		// The application delegate driving this app. getDelegate<T>() hands it back as
		// the concrete type the app was constructed with — a static_cast (the caller
		// knows that type from having wired it), guarded by a debug assert. A window
		// delegate reaches app-level state through window.app().getDelegate<MyApp>().
		ApplicationDelegate& delegate();
		template <typename T>
		T& getDelegate()
		{
			assert(dynamic_cast<T*>(&delegate()) != nullptr && "getDelegate<T>: delegate is not a T");
			return static_cast<T&>(delegate());
		}

		// The current input snapshot (also passed to onUpdate).
		const InputState& input() const;

		// The app's identity (name + version), as constructed.
		const AppInfo& info() const;

		// The -v/--verbose count from the command line (0 = none, 1 = -v, 2 = -vv, …),
		// valid once argv is parsed (i.e. from onStart onward). run() already maps it
		// onto the log level; this exposes the raw count for an app that wants to gate
		// its own behaviour on it.
		int verbosity() const;

	private:
		// Lazy subsystem bring-up, on demand: GLFW, the Vulkan instance, and the one
		// shared device (the device picks a GPU/queue that can present to `present`).
		void ensureGlfw();
		void ensureInstance();
		acm::Device& ensureDevice(const acm::Surface& present);

		// Snapshot the focused window's keyboard/mouse into the input state each frame.
		void updateInput();

		struct impl;
		std::unique_ptr<impl> m;
	};
} // namespace lain::app
