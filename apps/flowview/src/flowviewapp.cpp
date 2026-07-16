#include "flowviewapp.h"

#include "graphio.h"
#include "runmode.h"
#include "scene.h"

#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/graph.h>
#include <lain/io/image/codecs.h>

#include <cstdint>
#include <memory>

namespace flowview
{
	using namespace lain;

	bool FlowviewApp::onInit(app::Application&, app::cli::App& cli)
	{
		cli.add_option("--size", m_size, "example gradient extent (NxN)")->capture_default_str();
		cli.add_option("--frames", m_frames, "gui-mode: quit after N frames (0 = run until the window closes)")->capture_default_str();
		cli.add_flag("--example", m_useExample, "gui: start from the example scene instead of a blank Input/Output graph");
		cli.add_flag("--reset-layout", m_resetLayout, "gui: ignore the saved dock layout and start from the default");

		// Headless subcommands. `run`'s boundary bindings arrive as extras (--<name> <value>) — the
		// graph's interface isn't known until it's loaded, so they can't be registered up front;
		// `list` prints exactly those flag names.
		// --graph is an OPTION, not a positional: with allow_extras below, a bare positional would
		// greedily swallow a boundary flag's value (`run --result out.png` -> graph "out.png"). An
		// option can't, so the pretty --<boundary> <value> bindings stay unambiguous.
		m_runCmd = cli.add_subcommand("run", "headless: load a graph (or the example), bind its boundary inputs from --<name> <value>, run it, dump it, write bound outputs");
		m_runCmd->add_option("--graph,-g", m_graphPath, "graph JSON file (omit for the built-in example)");
		m_runCmd->add_option("--save", m_savePath, "also serialize the graph to this JSON path");
		m_runCmd->allow_extras(); // --<boundary> <value> pairs, matched to the loaded graph

		m_listCmd = cli.add_subcommand("list", "headless: print a graph's boundary inputs/outputs (the --<name> flags `run` accepts)");
		m_listCmd->add_option("--graph,-g", m_graphPath, "graph JSON file (omit for the built-in example)");

		return true;
	}

	bool FlowviewApp::onStart(app::Application& app)
	{
		if (m_runCmd->parsed() || m_listCmd->parsed())
			return true; // a headless subcommand: open no window -> run() invokes onProcess once

		app::WindowSpec spec;
		spec.title = "flowview";
		spec.width = 1280;
		spec.height = 720;
		// The InspectorWindow reaches this delegate (and its graph) via window.app(), so
		// there is nothing to wire here beyond handing it to the window.
		app.createWindow(spec, m_window); // creates the shared device; builds the gui Context

		// Populate the node palette + the image codecs (so a palette-added LoadImageNode can decode),
		// then build the starting scene and run it. Default is a blank Input/Output graph; --example
		// loads the demo pipeline (source -> tint -> blur) with its input bound to a gradient. CPU nodes.
		lain::io::image::registerImageCodecs();
		registerExampleNodes(m_nodeFactory, m_size);
		registerSceneSerialization(); // image::Image port type (the boundary ± menu) + json codec
		m_graph = std::make_unique<flow::Graph>();
		if (m_useExample)
		{
			buildExampleScene(*m_graph, m_nodeFactory);
			bindDefaultInput(*m_graph, m_size);
		}
		else
		{
			buildNewScene(*m_graph);
		}
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
		// Headless dispatch for the run/list subcommands. Pure CPU, no device. (onProcess can also
		// be reached via Application::process() in gui-mode — a no-op here without a subcommand.)
		if (!m_runCmd->parsed() && !m_listCmd->parsed())
			return;

		lain::io::image::registerImageCodecs();
		registerExampleNodes(m_nodeFactory, m_size);
		registerSceneSerialization();
		BoundaryBinders binders;
		registerBoundaryBinders(binders);

		if (m_listCmd->parsed())
		{
			listGraph(m_graphPath, m_nodeFactory, binders);
			return;
		}
		runGraph(m_graphPath, m_savePath, m_runCmd->remaining(), m_nodeFactory, binders, m_size);
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

	void FlowviewApp::replaceGraph(std::unique_ptr<flow::Graph> graph)
	{
		m_graph = std::move(graph);
		bindDefaultInput(*m_graph, m_size); // a loaded scene has no bound input — show a gradient until rebound
		m_scheduler.run(*m_graph);
	}
} // namespace flowview
