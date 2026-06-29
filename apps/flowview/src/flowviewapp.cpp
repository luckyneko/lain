#include "flowviewapp.h"

#include "dump.h"
#include "scene.h"

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/graph.h>

#include <iostream>

namespace flowview
{
	bool FlowviewApp::onInit(lain::app::Application&, lain::app::cli::App& cli)
	{
		cli.add_flag("--headless,-c", m_headless, "evaluate the example graph and dump its output (no window)");
		cli.add_option("--size", m_size, "example texture extent (NxN)")->capture_default_str();
		return true;
	}

	bool FlowviewApp::onStart(lain::app::Application& app)
	{
		if (m_headless)
			return true; // open no window -> headless: run() invokes onProcess once

		lain::app::WindowSpec spec;
		spec.title = "flowview";
		spec.width = 1280;
		spec.height = 720;
		app.createWindow(spec, m_window);
		return true;
	}

	void FlowviewApp::onProcess(lain::app::Application& app)
	{
		acm::Device device = app.device();

		lain::flow::Graph graph;
		const lain::flow::NodeId textureNode = buildExampleScene(graph, device, m_size);
		graph.evaluate(textureNode); // pull: runs the GPU source's compute()

		dumpGraph(std::cout, graph, device);
	}

	void FlowviewApp::ClearWindow::onRender(lain::app::Window& window, const lain::app::TimeState&)
	{
		window.renderer().render([](acm::CommandBuffer, uint32_t) {});
	}
} // namespace flowview
