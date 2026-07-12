#include "flowviewapp.h"

#include "dump.h"
#include "graphio.h"
#include "scene.h"

#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/portvalue.h>
#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/log/log.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

namespace flowview
{
	using namespace lain;

	bool FlowviewApp::onInit(app::Application&, app::cli::App& cli)
	{
		cli.add_flag("--headless,-c", m_headless, "run the example graph and dump its output (no window)");
		cli.add_option("--size", m_size, "default gradient extent (NxN)")->capture_default_str();
		cli.add_option("--frames", m_frames, "gui-mode: quit after N frames (0 = run until the window closes)")->capture_default_str();
		cli.add_option("--input", m_inputPath, "cli-mode: bind the graph's input boundary to this image file");
		cli.add_option("--output", m_outputPath, "cli-mode: write the graph's output boundary to this path");
		cli.add_option("--load-graph", m_loadGraphPath, "cli-mode: load the scene from this JSON graph (else the example)");
		cli.add_option("--save-graph", m_saveGraphPath, "cli-mode: serialize the scene to this JSON graph");
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

		// Populate the node palette + the image codecs (so a palette-added LoadImageNode can
		// decode), then build the boundary scene, bind its input to a default gradient (until
		// the Interface panel), and run it. The nodes are pure CPU.
		lain::io::image::registerImageCodecs();
		registerExampleNodes(m_nodeFactory, m_size);
		registerSceneSerialization(); // image::Image port type (the boundary ± menu) + json codec
		m_graph = std::make_unique<flow::Graph>();
		buildExampleScene(*m_graph, m_nodeFactory);
		bindDefaultInput(*m_graph, m_size);
		m_scheduler.run(*m_graph);
		return true;
	}

	void FlowviewApp::onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&)
	{
		if (m_frames > 0 && time.frame >= static_cast<std::uint64_t>(m_frames))
			app.quit();
	}

	void FlowviewApp::onProcess(app::Application&)
	{
		// cli-mode: build (or load) the boundary scene, bind its input from --input, run, dump,
		// then write its output to --output — the host-binds-the-boundary path, headless. Pure
		// CPU, no device. This is the write library's caller: io::image::load in, save out.
		lain::io::image::registerImageCodecs();
		registerExampleNodes(m_nodeFactory, m_size);
		registerSceneSerialization();

		flow::Graph graph;
		if (!m_loadGraphPath.empty())
		{
			auto result = loadGraph(m_loadGraphPath, m_nodeFactory);
			if (!result.clean())
				log::warn("flowview: graph loaded with {} issue(s)", result.issues.size());
			graph = std::move(result.graph);
		}
		else
		{
			buildExampleScene(graph, m_nodeFactory);
		}

		// Bind the graph's input boundary: --input loads a real file, else a default gradient
		// stands in so a bare `--headless` still produces something to dump.
		if (!m_inputPath.empty())
		{
			if (auto image = lain::io::image::load(m_inputPath))
			{
				flow::PortValue v;
				v.set<image::Image>(std::move(*image));
				const auto inputs = graph.boundaryInputs();
				if (!inputs.empty())
					inputs[0].setValue(std::move(v));
			}
			else
				log::error("flowview: could not load --input image: {}", m_inputPath);
		}
		else
		{
			bindDefaultInput(graph, m_size);
		}

		m_scheduler.run(graph);
		dumpGraph(std::cout, graph);

		// Serialize the scene (recipe: kinds + params + dynamic pins + edges) to --save-graph.
		if (!m_saveGraphPath.empty())
		{
			if (saveGraph(m_saveGraphPath, graph, m_nodeFactory))
				log::info("flowview: wrote graph to {}", m_saveGraphPath);
			else
				log::error("flowview: could not write graph to {}", m_saveGraphPath);
		}

		// Write the graph's output boundary to --output (the "proper write step").
		if (!m_outputPath.empty())
		{
			const auto outputs = graph.boundaryOutputs();
			if (!outputs.empty() && outputs[0].value().holds<image::Image>())
			{
				if (lain::io::image::save(m_outputPath, outputs[0].value().get<image::Image>()))
					log::info("flowview: wrote output to {}", m_outputPath);
				else
					log::error("flowview: could not write --output: {}", m_outputPath);
			}
			else
			{
				log::error("flowview: no image at the output boundary to write");
			}
		}
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
