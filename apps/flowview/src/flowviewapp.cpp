#include "flowviewapp.h"

#include "graphio.h"
#include "runmode.h"
#include "scene.h"

#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/graph.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/writer.h> // Compression — the --compression option's names come from the enum
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/io/video/writer.h>
#include <lain/log/log.h>
#include <lain/meta/enums.h>

#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// An enum option's accepted names, straight from the enum, lowercased so they read like a
	// command line rather than like C++ identifiers. Derived rather than typed out: a hand-written
	// list is a second place the set of values lives, and it goes stale silently.
	template <typename E>
	static std::vector<std::string> enumNames()
	{
		std::vector<std::string> names;
		for (const auto& [name, value] : lain::meta::enums::nameValueMap<E>())
		{
			(void)value;
			std::string lowered = name;
			for (char& c : lowered)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			names.push_back(std::move(lowered));
		}
		return names;
	}

	bool FlowviewApp::onInit(app::Application&, app::cli::App& cli)
	{
		cli.add_option("--size", m_size, "extent (NxN) of a NEW gradient and of the stand-in bound to an unbound image input; a saved gradient keeps its own size")->capture_default_str();
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

		// A codec FAMILY, never an encoder name: "h264_videotoolbox" is a fact about one machine,
		// so a command line carrying it stops working on the next one — which is what ADR-0019's
		// "never by a hardcoded name" forbids. The names come from the enum itself, so this list
		// cannot drift from what the writer accepts.
		m_runCmd->add_option("--codec", m_videoCodec,
							 "codec family for a video-valued output: auto (delivery, or refuse) | h264 | hevc | "
							 "prores | ffv1 (lossless) | mjpeg")
			// IsMember VALIDATES and leaves the string alone; CheckedTransformer would rewrite it to
			// the enum's underlying NUMBER, which runmode then fails to parse and — before this was
			// caught by running the real binary — silently fell back to auto. Asking for ffv1 and
			// getting h264 is precisely the substitution this option exists to prevent, so the
			// value that travels is the name.
			->check(app::cli::IsMember(enumNames<lain::io::video::VideoCodec>(), app::cli::ignore_case));

		// TYPED, like --frame: media::FrameRate provides CLI11's lexical_cast hook, so a malformed
		// rate is refused by the parser before a graph loads. Decimals are refused on purpose —
		// "29.97" is a rounded rendering of 30000/1001, and a rate that has been through a decimal
		// cannot go back into a container's timebase without drifting.
		m_runCmd->add_option("--rate", m_outputRate,
							 "output frame rate for a video output: 24 | 30000/1001 (default: the bound sequence's)");

		// How a STILL output is encoded. Both are the job rather than a codec's own number, which is
		// what lets one spelling serve png, tiff and jpeg — and what keeps a command line that works
		// here working after a format joins. The names come from the enum, like --codec's.
		m_runCmd->add_option("--compression", m_compression,
							 "how hard a still-image output squeezes: default | none | fast | small")
			->check(app::cli::IsMember(enumNames<lain::io::image::Compression>(), app::cli::ignore_case));

		// Fidelity, and ONLY for a lossy format — a png ignores it, and runmode says so once rather
		// than leaving a caller to wonder. Range-checked by the parser, so 0 and 101 are refused
		// before a graph loads; unset is what asks for the codec's own default, which is why the
		// value here is read through count() below rather than compared against a sentinel.
		m_runCmd->add_option("--quality", m_quality, "fidelity for a lossy still-image output: 1-100 (jpeg)")
			->check(app::cli::Range(1, 100));

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
		lain::io::video::registerVideoCodecs();		   // no-op unless a video codec plugin is built
		lain::io::sequence::registerSequenceOpeners(); // which media OpenSequence can dispatch to
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
		m_evaluation = std::make_shared<flow::Evaluation>(*m_graph);
		if (m_useExample)
			for (const flow::BoundaryInput& in : m_graph->boundaryInputs())
				bindDefaultInput(in, *m_evaluation, m_size);
		// Asked for rather than run: the first frame starts it, off the frame loop like every other.
		// (A new document's trigger is Live. A session reopening a Manual document swaps it in at the
		// end of the first frame, before the pump — so that document does not run on launch.)
		requestRun();
		return true;
	}

	void FlowviewApp::onUpdate(app::Application& app, const app::TimeState& time, const app::InputState&)
	{
		if (m_frames > 0 && time.frame >= static_cast<std::uint64_t>(m_frames))
			app.exit();
	}

	int FlowviewApp::onProcess(app::Application&)
	{
		// Headless dispatch for the run/list subcommands. Pure CPU, no device. (onProcess can also
		// be reached via Application::process() in gui-mode — a no-op here without a subcommand.)
		if (!m_runCmd->parsed() && !m_listCmd->parsed())
			return 0;

		lain::io::image::registerImageCodecs();
		lain::io::video::registerVideoCodecs();
		lain::io::sequence::registerSequenceOpeners();
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
		options.videoCodec = m_videoCodec;
		// Presence, not emptiness — the same reason --frame uses count(): an unspecified rate and
		// a rate that happens to be zero are different requests, and only one of them is an error.
		if (m_runCmd->count("--rate") > 0)
			options.outputRate = m_outputRate;
		options.compression = m_compression;
		// Presence again, for the reason the option's own comment gives: "no quality asked for" is
		// what reaches the codec's default, and it is not the same request as any number.
		if (m_runCmd->count("--quality") > 0)
			options.quality = m_quality;
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
		// The run in flight goes FIRST: it participates in the task pool, which stops after the last
		// onShutdown, and it may be reading the evaluation and a clone of the document. stop() cancels
		// and JOINS it — the drain is bounded by the compute in progress, and this is the one place
		// the UI thread is allowed to wait on graph work, since there is no longer a frame to keep.
		m_runner.stop();

		// Then what the panes read, and the evaluation it was copied from: they are where the payloads
		// actually live.
		m_published = flow::PublishedEvaluation{};
		m_bindings.clear();
		m_evaluation.reset();
		m_graph.reset();
	}

	void FlowviewApp::requestRun()
	{
		m_requests.changed();
	}

	void FlowviewApp::runNow()
	{
		m_requests.runNow();
	}

	void FlowviewApp::runSelection(std::vector<flow::NodeId> targets)
	{
		m_requests.runSelection(std::move(targets));
	}

	void FlowviewApp::bind(flow::PortAddress input, flow::PortValue value)
	{
		// Shown now, applied later: the published copy is what the panes read this frame, and the
		// queue is what the next run consumes.
		m_published.bind(input, value);
		m_bindings.set(input, std::move(value));
		requestRun();
	}

	void FlowviewApp::stopRun()
	{
		m_runner.cancel();
		m_requests.clear();
	}

	bool FlowviewApp::pollRun()
	{
		RunReport report = m_runner.take();
		if (report.outcome)
			m_lastRun = LastRun{*report.outcome, m_runStarted.since().seconds(), report.failedNodes, report.partial};
		if (report.outcome == RunOutcome::Completed)
			m_runFailure.reset();
		else if (report.outcome == RunOutcome::Failed)
		{
			log::error("flowview: the run failed: {}", report.failure);
			// A throw some node owned is on that node — recorded in the evaluation this run published,
			// so the canvas marks it and Issues lists it where it happened. Only a throw nobody owned
			// has to be reported for the run as a whole.
			if (report.failedNodes == 0)
				m_runFailure = std::move(report.failure);
			else
				m_runFailure.reset();
		}

		m_activity = std::move(report.activity);

		// A new publication, then what has landed since it node by node (M14 slice 7) — each result
		// folded in, each node the run still owes marked so, which is what keeps the copy's staleness
		// sound while it fills in piecemeal — and the pending bindings laid back over the top, since a
		// binding no copy can contain yet would otherwise read back as its old value.
		return land(report, m_published, m_publishedBy, m_bindings);
	}

	void FlowviewApp::pumpRun(RunTrigger trigger, bool gestureEnded)
	{
		if (!m_graph || !m_requests.due(trigger, gestureEnded))
			return;

		// SUPERSEDE: the run in flight is computing against a document that has since changed. Cancel
		// it and keep the asks; once it has drained, a later frame starts the new one. (Waiting for it
		// here is what this whole arrangement exists not to do.)
		if (m_runner.busy())
		{
			m_runner.cancel();
			return;
		}

		RunJob job;
		// The clone is the ONE place a run meets the document, and it is taken here, on the thread
		// that edits it, between frames — so no edit can race it (ADR-0025).
		job.definition = std::make_shared<const flow::Graph>(m_graph->clone());
		job.evaluation = m_evaluation;
		job.bindings = m_bindings.handOver(m_runner.nextJob());
		job.strategy = m_strategy;
		job.scope = m_requests.scope(trigger, gestureEnded);
		const RunScope scope = job.scope; // what the run answers, kept past the hand-over
		m_runner.start(std::move(job));
		m_runStarted = core::Time::now();
		m_requests.started(scope);
	}

	void FlowviewApp::replaceGraph(std::unique_ptr<flow::Graph> graph)
	{
		// A run of the old document may still be in flight. It is dropped, not waited for: it holds
		// its own clone and a share of the old evaluation, so it drains and dies with its job — and
		// whatever it would still publish belongs to a lineage the panes are no longer showing.
		m_runner.abandon();

		// BOTH or neither: a new definition gets a new evaluation, because the old one's records
		// belong to a graph that no longer exists (ADR-0012). Making the swap atomic here is what
		// makes the pairing structural rather than something to remember.
		m_graph = std::move(graph);
		m_evaluation = std::make_shared<flow::Evaluation>(*m_graph);
		// A loaded scene has nothing bound — show a gradient on every image input it has. Directly into
		// the evaluation, which is safe exactly here: no run has seen this one yet.
		for (const flow::BoundaryInput& in : m_graph->boundaryInputs())
			bindDefaultInput(in, *m_evaluation, m_size);

		// The panes show nothing until the new document's first run publishes, rather than the old
		// document's values: those belong to a dead lineage, and would pass for the new one's.
		m_published = flow::PublishedEvaluation{};
		m_publishedBy = 0;
		m_bindings.clear();
		m_activity = RunActivity{};
		m_runFailure.reset();
		m_lastRun.reset(); // how the old document's last run went says nothing about this one
		requestRun();
	}
} // namespace flowview
