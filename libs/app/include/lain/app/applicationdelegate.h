#pragma once

#include <lain/app/cli.h>
#include <lain/app/inputstate.h>
#include <lain/app/timestate.h>

namespace lain::app
{
	class Application;

	// Application-wide behaviour, all on the main thread.
	class ApplicationDelegate
	{
	public:
		virtual ~ApplicationDelegate() = default;

		// Earliest hook, before the instance/device. Register CLI options on `cli`
		// (CLI11's API; the base app owns parsing) and read app config. Return false
		// to abort.
		virtual bool onInit([[maybe_unused]] Application& app, [[maybe_unused]] cli::App& cli) { return true; }

		// After argv is parsed: create windows (Application::createWindow) and any
		// app-wide state. Opening no windows selects headless. Return false to abort.
		virtual bool onStart(Application& app) = 0;

		// Per-frame UI / animation in gui-mode, vsync-paced. Read input here.
		virtual void onUpdate([[maybe_unused]] Application& app, [[maybe_unused]] const TimeState& time, [[maybe_unused]] const InputState& input) {}

		// The work routine — graph evaluation / processing, kept separate from the
		// frame. Invoked once by run() in headless-mode; in gui-mode it runs only when
		// something calls Application::process() (e.g. a "Run" button), never polled.
		virtual void onProcess([[maybe_unused]] Application& app) {}

		// App stopping: release app-wide resources before windows/device tear down.
		virtual void onStop([[maybe_unused]] Application& app) {}

		// Final teardown, after windows + device are gone (symmetric to onInit).
		virtual void onShutdown([[maybe_unused]] Application& app) {}
	};
}
