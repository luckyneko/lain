#pragma once

#include <archimedes/acmSampler.h>
#include <lain/app/windowdelegate.h>
#include <lain/gui/context.h>

#include <memory>

namespace flowview
{
	// Per-window ImGui inspector. Owns only its GUI resources (the lain::gui Context + a
	// sampler); it holds no app/graph state. Each hook reaches what it needs from its
	// Window argument — window.app() for the Application, and
	// window.app().getDelegate<FlowviewApp>().graph() for the scene it draws. The panel
	// shows each node's ports as text and an acm::Texture output as a live thumbnail.
	// ImGui is single-threaded — every call here is on the main/render thread.
	class InspectorWindow : public lain::app::WindowDelegate
	{
	public:
		bool onInit(lain::app::Window& window) override;
		void onRender(lain::app::Window& window, const lain::app::TimeState& time) override;
		void onShutdown(lain::app::Window& window) override;

	private:
		std::unique_ptr<lain::gui::Context> m_guiCtx;
		acm::Sampler m_sampler;
		ImTextureID m_preview{}; // cached texture descriptor (registered once)
		bool m_havePreview = false;
		bool m_laidOut = false; // node canvas: seed node positions on the first frame
	};
} // namespace flowview
