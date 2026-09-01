#pragma once

#include "lain/app/appinfo.h" // AppInfo (name + version identity)
#include "lain/app/window.h"  // WindowSpec + Window (createWindow return)

#include <archimedes/acmForward.h>

#include <cassert>
#include <memory>

namespace lain::app
{
	class ApplicationDelegate;
	class WindowDelegate;
	struct InputState;

	// Owns the Vulkan instance, the one shared acm::Device, the windows it creates,
	// and the run loop. Driven by an ApplicationDelegate; each window by its own
	// WindowDelegate. Construct one, run() it. Sole owner (non-copyable); delegates
	// are borrowed — you keep them alive past run().
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

		// onInit(cli) -> parse argv -> onStart -> then:
		//   gui-mode (windows opened): the frame loop (onUpdate + render each window)
		//     until every window closes or quit() is called;
		//   headless (no windows): onProcess once.
		// -> onStop -> device+window teardown -> onShutdown. Returns an exit code.
		int run(int argc, char** argv);

		// Invoke the delegate's onProcess (the work routine) and return its status — 0 for success.
		// run() calls this once in headless-mode and reports the status as the process exit code;
		// in gui-mode a delegate calls it on demand (e.g. a "Run" button) and can act on the result.
		int process();

		// Ask the gui loop to exit after the current iteration, reporting success. exit(code) is
		// the same request with a status.
		void quit();

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

		// End the run with `code` — quit() that also says why.
		//
		// The gui counterpart of onProcess's return value: a windowed app has no single work
		// routine whose status could stand for the run, so a delegate that concludes it has failed
		// says so here. Takes effect after the current loop iteration, exactly as quit() does; in
		// headless mode it sets the status that run() returns.
		//
		// Known gap, pre-existing: run()'s early exits — onInit or onStart returning false, a CLI
		// parse error, --licenses — return their own fixed code and never reach this, so a status
		// set before one of them is discarded.
		void exit(int code);

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
