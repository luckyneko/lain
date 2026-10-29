#pragma once

#include "lain/app/timestate.h"

#include <archimedes/acmForward.h>
#include <archimedes/acmTypes.h>

namespace lain::app
{
	class Window;

	// Per-window behaviour. onRender currently runs on the main thread (the App is
	// single-threaded for now), but treat it as render-only — record + present via
	// the window's renderer; read app state through onUpdate, not here.
	class WindowDelegate
	{
	public:
		virtual ~WindowDelegate() = default;

		// The window's device + swapchain are ready: build per-window pipelines /
		// targets. Return false to abort the window's creation.
		virtual bool onInit([[maybe_unused]] Window& window) { return true; }

		// Record + present this window's frame — typically:
		//   window.renderer().render([&](acm::CommandBuffer cmd, uint32_t frame){ ... });
		virtual void onRender(Window& window, const TimeState& time) = 0;

		// The swapchain was rebuilt at a new size (resize). Main thread.
		virtual void onResize([[maybe_unused]] Window& window, [[maybe_unused]] acm::Extent2D extent) {}

		// The window was asked to close: its close button, the platform's own shortcut for it, or
		// Window::requestClose. Return true to let it close, which ends the frame loop; return false to
		// keep it open, as an editor does while it asks whether to save, and the loop carries on as
		// though nothing had been asked. Asked once per request, at the top of a frame. Not asked when
		// the app ends the run itself (Application::exit), which is the delegate's own decision already.
		virtual bool onCloseRequested([[maybe_unused]] Window& window) { return true; }

		// Release per-window GPU resources before the window/device tear down. The GPU is idle by
		// now — every frame the loop submitted has FINISHED, not merely been submitted — so this
		// may free anything a frame read, including objects acm does not own and so cannot defer
		// to a completed frame (ImGui's backend, torn down by gui::Context's destructor).
		virtual void onShutdown([[maybe_unused]] Window& window) {}
	};
} // namespace lain::app
