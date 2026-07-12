#pragma once

#include "inspectorwindow.h"

#include <lain/app/applicationdelegate.h>
#include <lain/core/factory.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/scheduler.h>
#include <lain/flow/types.h>

#include <cstdint>
#include <memory>
#include <string>

namespace flowview
{
	// flowview's application delegate. Two modes, selected by CLI:
	//   --headless : cli-mode — build the boundary example graph, bind its input from
	//                --input (if given), run it, dump the graph, and write its output to
	//                --output (if given). Opens no window, so the Application runs
	//                onProcess once and exits.
	//   default    : gui-mode — open a window and show the InspectorWindow (ports as text,
	//                image outputs as thumbnails) + the node canvas. The graph's input is
	//                bound to a default gradient until the Interface panel (next commit).
	class FlowviewApp : public lain::app::ApplicationDelegate
	{
	public:
		bool onInit(lain::app::Application& app, lain::app::cli::App& cli) override;
		bool onStart(lain::app::Application& app) override;
		void onUpdate(lain::app::Application& app, const lain::app::TimeState& time, const lain::app::InputState& input) override;
		void onProcess(lain::app::Application& app) override;
		void onStop(lain::app::Application& app) override;

		// The gui-mode scene the InspectorWindow reads (reached via
		// window.app().getDelegate<FlowviewApp>().graph()). Valid in gui-mode (built in
		// onStart, released in onStop).
		const lain::flow::Graph& graph() const { return *m_graph; }
		lain::flow::Graph& graph() { return *m_graph; } // the canvas edits it in place

		// The node-type palette the canvas' add menu draws from.
		const lain::core::Factory<lain::flow::Node>& nodeFactory() const { return m_nodeFactory; }

		// Re-run the scene after a canvas edit so data flows through the current
		// wiring. Called from the render thread.
		void reevaluate();

		// Replace the gui-mode scene with a freshly loaded graph (from the canvas Load…), bind its
		// input to a default gradient so it shows a result, and run it. Called from the render thread.
		void replaceGraph(std::unique_ptr<lain::flow::Graph> graph);

	private:
		bool m_headless = false;
		std::uint32_t m_size = 64; // default gradient extent (size x size)
		int m_frames = 0;		   // gui-mode: quit after N frames (0 = until closed)
		std::string m_inputPath;	 // cli-mode: bind the graph's input boundary to this image
		std::string m_outputPath;	 // cli-mode: write the graph's output boundary to this path
		std::string m_loadGraphPath; // cli-mode: load the scene from this JSON graph (else build the example)
		std::string m_saveGraphPath; // cli-mode: serialize the scene to this JSON graph

		// The gui-mode scene, held by unique_ptr so onStop can release it (and its
		// node-owned payloads) explicitly, before the window/device teardown.
		std::unique_ptr<lain::flow::Graph> m_graph;
		lain::flow::SerialScheduler m_scheduler;			 // runs the scene (no threads needed)
		lain::core::Factory<lain::flow::Node> m_nodeFactory; // node-type palette
		InspectorWindow m_window;							 // gui-mode inspector
	};
} // namespace flowview
