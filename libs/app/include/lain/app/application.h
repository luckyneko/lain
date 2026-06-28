#pragma once

#include <memory>

#include <archimedes/acmForward.h>

#include <lain/app/window.h> // WindowSpec + Window (createWindow return)

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
		explicit Application(ApplicationDelegate& delegate);
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

		// Invoke the delegate's onProcess (the work routine). run() calls this once in
		// headless-mode; in gui-mode a delegate calls it on demand (e.g. a "Run" button).
		void process();

		// Ask the gui loop to exit after the current iteration.
		void quit();

		// The shared device, created on demand (for windows or headless compute).
		acm::Device device();

		// The current input snapshot (also passed to onUpdate).
		const InputState& input() const;

	private:
		struct impl;
		std::unique_ptr<impl> m;
	};
}
