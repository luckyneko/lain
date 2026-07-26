#pragma once

#include "mainwindow.h"

#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>
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
	//   default    : gui-mode — open a window and show the MainWindow (ports as text,
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

		// The gui-mode scene the MainWindow reads (reached via
		// window.app().getDelegate<FlowviewApp>().graph()). Valid in gui-mode (built in
		// onStart, released in onStop).
		const lain::flow::Graph& graph() const { return *m_graph; }
		lain::flow::Graph& graph() { return *m_graph; } // the canvas edits it in place

		// The node-type palette the canvas' add menu draws from.
		const lain::core::Factory<lain::flow::Node>& nodeFactory() const { return m_nodeFactory; }

		// --reset-layout: start from the default dock layout, ignoring any saved one (recovery hatch).
		bool resetLayout() const { return m_resetLayout; }

		// --example: start from the demo scene. An explicit ask, so it also suppresses reopening the
		// graph the last session had open.
		bool useExample() const { return m_useExample; }

		// Re-run the scene after a canvas edit so data flows through the current
		// wiring. Called from the render thread.
		void reevaluate();

		// Replace the gui-mode scene with a freshly loaded graph (from the canvas Load…), bind its
		// input to a default gradient so it shows a result, and run it. Called from the render thread.
		void replaceGraph(std::unique_ptr<lain::flow::Graph> graph);

	private:
		std::uint32_t m_size = 64;	// example gradient extent (size x size)
		int m_frames = 0;			// gui-mode: quit after N frames (0 = until closed)
		bool m_useExample = false;	// gui: --example starts from the example scene, else a blank graph
		bool m_resetLayout = false; // gui: --reset-layout ignores the saved dock layout
		std::string m_graphPath;	// run/list: the graph JSON (empty -> the built-in example scene)
		std::string m_savePath;		// run --save: serialize the graph here

		// The headless subcommands. Their pointers stay valid through Application::run() (the cli::App
		// outlives onStart/onProcess), so ->parsed()/->remaining() drive the headless dispatch.
		lain::app::cli::App* m_runCmd = nullptr;
		lain::app::cli::App* m_listCmd = nullptr;

		// The gui-mode scene, held by unique_ptr so onStop can release it (and its
		// node-owned payloads) explicitly, before the window/device teardown.
		std::unique_ptr<lain::flow::Graph> m_graph;
		lain::flow::SerialScheduler m_scheduler;			 // runs the scene (no threads needed)
		lain::core::Factory<lain::flow::Node> m_nodeFactory; // node-type palette
		MainWindow m_window;								 // gui-mode inspector
	};
} // namespace flowview
