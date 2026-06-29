#pragma once

#include <lain/app/applicationdelegate.h>
#include <lain/app/windowdelegate.h>

#include <cstdint>

namespace lain::app
{
	class Window;
}

namespace flowview
{
	// flowview's application delegate. Two modes, selected by CLI:
	//   --headless : build the example graph, evaluate it, and dump the result to
	//                stdout (cli-mode); opens no window, so the Application runs
	//                onProcess once and exits.
	//   default    : gui-mode — opens a window. The lain::gui inspector panels land
	//                in the next step; for now the window just clears.
	class FlowviewApp : public lain::app::ApplicationDelegate
	{
	public:
		bool onInit(lain::app::Application& app, lain::app::cli::App& cli) override;
		bool onStart(lain::app::Application& app) override;
		void onProcess(lain::app::Application& app) override;

	private:
		bool m_headless = false;
		std::uint32_t m_size = 64; // example texture extent (size x size)

		// gui-mode placeholder: a window that just clears. Replaced by the lain::gui
		// inspector + node canvas in the next step.
		class ClearWindow : public lain::app::WindowDelegate
		{
			void onRender(lain::app::Window& window, const lain::app::TimeState& time) override;
		} m_window;
	};
} // namespace flowview
