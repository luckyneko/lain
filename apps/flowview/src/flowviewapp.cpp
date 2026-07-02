#include "flowviewapp.h"

#include "dump.h"
#include "scene.h"

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/graph.h>

#include <cstdint>
#include <iostream>

namespace flowview
{
	using namespace lain;

	bool FlowviewApp::onInit(app::Application&, app::cli::App& cli)
	{
		cli.add_flag("--headless,-c", m_headless, "evaluate the example graph and dump its output (no window)");
		cli.add_option("--size", m_size, "example texture extent (NxN)")->capture_default_str();
		cli.add_option("--frames", m_frames, "gui-mode: quit after N frames (0 = run until the window closes)")->capture_default_str();
		return true;
	}

	bool FlowviewApp::onStart(app::Application& app)
	{
		if (m_headless)
			return true; // open no window -> headless: run() invokes onProcess once

		app::WindowSpec spec;
		spec.title = "flowview";
		spec.width = 1280;
		spec.height = 720;
		// The InspectorWindow reaches this delegate (and its graph) via window.app(), so
		// there is nothing to wire here beyond handing it to the window.
		app.createWindow(spec, m_window); // creates the shared device; builds the gui Context

		// Device is live now: populate the node palette, then build + evaluate the
		// smoke scene the inspector reads.
		registerExampleNodes(m_nodeFactory, app.device(), m_size);
		m_textureNode = buildExampleScene(m_graph, m_nodeFactory);
		m_scheduler.evaluate(m_graph, m_textureNode); // pull: runs the GPU source's compute()
		return true;
	}

	void FlowviewApp::onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&)
	{
		if (m_frames > 0 && time.frame >= static_cast<std::uint64_t>(m_frames))
			app.quit();
	}

	void FlowviewApp::onProcess(app::Application& app)
	{
		acm::Device device = app.device();

		registerExampleNodes(m_nodeFactory, device, m_size);
		flow::Graph graph;
		const flow::NodeId textureNode = buildExampleScene(graph, m_nodeFactory);
		m_scheduler.evaluate(graph, textureNode); // pull: runs the GPU source's compute()

		dumpGraph(std::cout, graph, device);
	}

	void FlowviewApp::reevaluate()
	{
		// A topology edit doesn't dirty nodes, so force a full recompute through the
		// current wiring, then pull the sink.
		for (const flow::NodeId id : m_graph.topoOrder())
			m_graph.node(id).markDirty();
		m_scheduler.evaluate(m_graph, m_textureNode);
	}
} // namespace flowview
