#include "flowviewapp.h"

#include "dump.h"
#include "scene.h"

#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/graph.h>
#include <lain/io/image/codecs.h>

#include <cstdint>
#include <iostream>
#include <memory>

namespace flowview
{
	using namespace lain;

	bool FlowviewApp::onInit(app::Application&, app::cli::App& cli)
	{
		cli.add_flag("--headless,-c", m_headless, "evaluate the example graph and dump its output (no window)");
		cli.add_option("--size", m_size, "example texture extent (NxN)")->capture_default_str();
		cli.add_option("--frames", m_frames, "gui-mode: quit after N frames (0 = run until the window closes)")->capture_default_str();
		cli.add_option("--image", m_imagePath, "cli-mode: load this image file through a LoadImageNode and dump it");
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

		// Populate the node palette, then build + evaluate the smoke scene the inspector
		// reads. The nodes are pure CPU, so no device is threaded through here.
		registerExampleNodes(m_nodeFactory, m_size);
		m_graph = std::make_unique<flow::Graph>();
		m_textureNode = buildExampleScene(*m_graph, m_nodeFactory);
		m_scheduler.evaluate(*m_graph, m_textureNode); // pull: runs the source's compute()
		return true;
	}

	void FlowviewApp::onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&)
	{
		if (m_frames > 0 && time.frame >= static_cast<std::uint64_t>(m_frames))
			app.quit();
	}

	void FlowviewApp::onProcess(app::Application&)
	{
		// cli-mode: a local graph, evaluated and dumped. Pure CPU — no device needed.
		flow::Graph graph;
		flow::NodeId sink{};

		if (!m_imagePath.empty())
		{
			// Load a real file through a LoadImageNode — the M3 file -> node -> output proof.
			// The reader registry must be populated first (the node just calls load()).
			lain::io::image::registerImageCodecs();
			sink = graph.add(std::make_unique<flow::example::LoadImageNode>(m_imagePath));
		}
		else
		{
			// Default smoke scene: gradient -> tint -> blur.
			registerExampleNodes(m_nodeFactory, m_size);
			sink = buildExampleScene(graph, m_nodeFactory);
		}

		m_scheduler.evaluate(graph, sink); // pull: runs the source's compute()
		dumpGraph(std::cout, graph);
	}

	void FlowviewApp::onStop(app::Application&)
	{
		// Release the gui-mode scene before the app tears the device down. Today's nodes
		// hold only CPU images (so the unique_ptr could just self-destruct later), but a
		// future node that owns GPU buffers must release them while the device is alive —
		// onStop runs before device teardown, so resetting here keeps that safe. Headless
		// mode uses a local graph, so m_graph is null here and this is a no-op.
		m_graph.reset();
	}

	void FlowviewApp::reevaluate()
	{
		// A full topo-order run (not a pull of one target) recomputes the whole graph
		// through its current wiring — correct no matter which nodes/edges were edited,
		// including deletion of whatever used to be the pulled sink.
		m_scheduler.run(*m_graph);
	}
} // namespace flowview
