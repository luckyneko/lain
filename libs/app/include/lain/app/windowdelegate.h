#pragma once

#include <archimedes/acmForward.h>
#include <archimedes/acmTypes.h>

#include <lain/app/timestate.h>

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

		// Release per-window GPU resources before the window/device tear down.
		virtual void onShutdown([[maybe_unused]] Window& window) {}
	};
}
