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

		// A RANGE where a binding takes a value, so the loop is a modifier on the one binding path
		// rather than a second subcommand duplicating the whole interface. Note --frames (no `s` on
		// this one) is a different, gui-only option on the root app: a frame cap, not a range.
		//
		// TYPED, not a string this app parses later: core::Range provides CLI11's lexical_cast hook,
		// so a malformed range is rejected by the parser with the rest of the command line — before
		// a graph is loaded — and there is no second place where the syntax could drift.
		m_runCmd->add_option("--frame", m_frameRange,
							 "render a range instead of a single run: 12 | 0-499 | 0-499x2 (needs a FramePosition input)");
		m_runCmd->add_option("--on-missing-frame", m_onMissingFrame,
							 "what a render does when a frame produces nothing: stop (default) | skip")
			->check(app::cli::IsMember({"stop", "skip"}));

		m_runCmd->allow_extras(); // --<boundary> <value> pairs, matched to the loaded graph

		m_listCmd = cli.add_subcommand("list", "headless: print a graph's boundary inputs/outputs (the --<name> flags `run` accepts)");
		m_listCmd->add_option("--graph,-g", m_graphPath, "graph JSON file (omit for the built-in example)");

		return true;
	}

	bool FlowviewApp::onStart(app::Application& app)
	{
		if (m_runCmd->parsed() || m_listCmd->parsed())
			return true; // a headless subcommand: open no window -> run() invokes onProcess once

		// Populate the node palette + the image codecs (so a palette-added LoadImageNode can decode) and
		// the serialization surfaces BEFORE the window: MainWindow::onInit reopens the last session's
		// graph, which needs the node factory and the json codec already registered.
		lain::io::image::registerImageCodecs();
		registerExampleNodes(m_nodeFactory, m_size);
		registerSceneSerialization(); // image::Image port type (the boundary ± menu) + json codec

		app::WindowSpec spec;
		spec.title = "flowview";
		spec.width = 1280;
		spec.height = 720;
		// The MainWindow reaches this delegate (and its graph) via window.app(), so
		// there is nothing to wire here beyond handing it to the window.
		app.createWindow(spec, m_window); // creates the shared device; builds the gui Context

		// The starting scene: a blank Input/Output graph, or the demo pipeline (source -> tint -> blur)
		// with its input bound to a gradient under --example. CPU nodes. A restored session graph
		// replaces this at the end of the first frame.
		m_graph = std::make_unique<flow::Graph>();
		if (m_useExample)
			buildExampleScene(*m_graph, m_nodeFactory);
		// else: a blank document needs no building — a fresh Graph is already one empty Input +
		// one empty Output node, and the user grows the interface from the Interface panel's ±.
		m_evaluation = flow::Evaluation{*m_graph};
		if (m_useExample)
			for (const flow::BoundaryInput& in : m_graph->boundaryInputs())
				bindDefaultInput(in, m_evaluation, m_size);
		m_scheduler.run(*m_graph, m_evaluation);
		return true;
	}

	void FlowviewApp::onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&)
	{
		if (m_frames > 0 && time.frame >= static_cast<std::uint64_t>(m_frames))
			app.quit();
	}

	int FlowviewApp::onProcess(app::Application&)
	{
		// Headless dispatch for the run/list subcommands. Pure CPU, no device. (onProcess can also
		// be reached via Application::process() in gui-mode — a no-op here without a subcommand.)
		if (!m_runCmd->parsed() && !m_listCmd->parsed())
			return 0;

		lain::io::image::registerImageCodecs();
		registerExampleNodes(m_nodeFactory, m_size);
		registerSceneSerialization();
		BoundaryBinders binders;
		registerBoundaryBinders(binders);

		// The returned status is the point: a render that stopped on a missing frame must be
		// distinguishable from a complete one by a caller that only sees the process exit code.
		if (m_listCmd->parsed())
			return listGraph(m_graphPath, m_nodeFactory, binders);

		RunOptions options;
		options.graphPath = m_graphPath;
		options.savePath = m_savePath;
		// Presence, not emptiness: CLI11 knows whether the option was given, and "--frame 0" is a
		// real one-frame render that an empty-vs-not check on a value would lose.
		if (m_runCmd->count("--frame") > 0)
			options.frameRange = m_frameRange;
		options.skipMissingFrames = m_onMissingFrame == "skip";
		options.bindings = m_runCmd->remaining();
		options.exampleSize = m_size;
		return runGraph(options, m_nodeFactory, binders);
	}

	void FlowviewApp::onStop(app::Application&)
	{
		// Release the gui-mode scene before the app tears the device down. Today's nodes
		// hold only CPU images (so the unique_ptr could just self-destruct later), but a
		// future node that owns GPU buffers must release them while the device is alive —
		// onStop runs before device teardown, so resetting here keeps that safe. Headless
		// mode uses a local graph, so m_graph is null here and this is a no-op.
		//
		// The evaluation goes FIRST and with it: it is where the payloads actually live now, and it
		// holds a pointer to the definition it belongs to.
		m_evaluation = flow::Evaluation{};
		m_graph.reset();
	}

	void FlowviewApp::reevaluate()
	{
		// A full topo-order run (not a pull of one target) recomputes the whole graph
		// through its current wiring — correct no matter which nodes/edges were edited,
		// including deletion of whatever used to be the pulled sink.
		m_scheduler.run(*m_graph, m_evaluation);
	}

	void FlowviewApp::replaceGraph(std::unique_ptr<flow::Graph> graph)
	{
		// BOTH or neither: a new definition gets a new evaluation, because the old one's recorded
		// per-node versions belong to a graph that no longer exists (ADR-0012). Making the swap
		// atomic here is what makes the pairing structural rather than something to remember.
		m_graph = std::move(graph);
		m_evaluation = flow::Evaluation{*m_graph};
		// A loaded scene has nothing bound — show a gradient on every image input it has.
		for (const flow::BoundaryInput& in : m_graph->boundaryInputs())
			bindDefaultInput(in, m_evaluation, m_size);
		m_scheduler.run(*m_graph, m_evaluation);
	}
} // namespace flowview
