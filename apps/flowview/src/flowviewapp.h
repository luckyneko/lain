#pragma once

#include "inspectorwindow.h"

#include <lain/app/applicationdelegate.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>
#include <lain/flow/types.h>

#include <cstdint>

namespace flowview
{
	// flowview's application delegate. Two modes, selected by CLI:
	//   --headless : build the example graph, evaluate it, and dump the result to
	//                stdout (cli-mode); opens no window, so the Application runs
	//                onProcess once and exits.
	//   default    : gui-mode — open a window and show the InspectorWindow: each
	//                node's ports as text, and an acm::Texture output as a thumbnail.
	//                (The imnodes node-canvas is deferred — WORK.md step 9.)
	class FlowviewApp : public lain::app::ApplicationDelegate
	{
	public:
		bool onInit(lain::app::Application& app, lain::app::cli::App& cli) override;
		bool onStart(lain::app::Application& app) override;
		void onUpdate(lain::app::Application& app, const lain::app::TimeState& time, const lain::app::InputState& input) override;
		void onProcess(lain::app::Application& app) override;

		// The gui-mode scene the InspectorWindow reads (reached via
		// window.app().getDelegate<FlowviewApp>().graph()).
		const lain::flow::Graph& graph() const { return m_graph; }

	private:
		bool m_headless = false;
		std::uint32_t m_size = 64; // example texture extent (size x size)
		int m_frames = 0;		   // gui-mode: quit after N frames (0 = until closed)

		lain::flow::Graph m_graph;			  // the gui-mode scene (persists across frames)
		lain::flow::NodeId m_textureNode{}; // the GPU source the scene is pulled from
		lain::flow::SerialScheduler m_scheduler; // pull-evaluates the scene (no threads needed)
		InspectorWindow m_window;
	};
} // namespace flowview
