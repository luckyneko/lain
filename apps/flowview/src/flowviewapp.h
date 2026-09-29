#pragma once

#include "mainwindow.h"
#include "runner.h"

#include <lain/app/applicationdelegate.h>
#include <lain/app/cli.h>
#include <lain/core/factory.h>
#include <lain/core/range.h>
#include <lain/core/time.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/portvalue.h>
#include <lain/flow/types.h>
#include <lain/media/framespec.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flowview
{
	// How the last run that got to an end went, for the status bar.
	struct LastRun
	{
		RunOutcome outcome = RunOutcome::Completed;
		double seconds = 0.0;		 // from the start the host asked for to the outcome it saw
		std::size_t failedNodes = 0; // node computes that threw (each recorded against its node)
		bool partial = false;		 // a Run Selection: what it did not reach is still Stale
	};

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
		int onProcess(lain::app::Application& app) override;
		void onStop(lain::app::Application& app) override;

		// The gui-mode scene the MainWindow reads (reached via
		// window.app().getDelegate<FlowviewApp>().graph()). Valid in gui-mode (built in
		// onStart, released in onStop).
		const lain::flow::Graph& graph() const { return *m_graph; }
		lain::flow::Graph& graph() { return *m_graph; } // the canvas edits it in place

		// What every pane READS: the newest published copy of the scene's runtime state — every port
		// value a run has produced, plus what is bound at the boundary (ADR-0025). Not the working
		// evaluation: a run owns that while it is in flight, on the coordinator thread, and a pane
		// reading it would race the run's writes. Refreshed by pollRun() as results land — whole at a
		// run's start, between stages and at its end, and node by node in between — so it can be a node,
		// a stage or a run behind the document; empty after a document swap until the new document's
		// first run starts.
		const lain::flow::Evaluation& published() const { return m_published.evaluation(); }

		// What the run in flight is doing to each node — Queued or Computing — as of the last
		// pollRun(). Empty while no run is in flight. Taken with the results that landed, so a node is
		// never shown idle before its result has.
		const RunActivity& activity() const { return m_activity; }

		// The node-type palette the canvas' add menu draws from.
		const lain::core::Factory<lain::flow::Node>& nodeFactory() const { return m_nodeFactory; }

		// --reset-layout: start from the default dock layout, ignoring any saved one (recovery hatch).
		bool resetLayout() const { return m_resetLayout; }

		// --example: start from the demo scene. An explicit ask, so it also suppresses reopening the
		// graph the last session had open.
		bool useExample() const { return m_useExample; }

		// Say that something changed — an edit, a binding, a group sync, a document swap — so the shown
		// values may no longer match. Only records it: pumpRun() decides at the end of the frame,
		// under the document's run trigger, whether that starts a run (RunRequests). UI thread.
		void requestRun();

		// An explicit Run: due under every trigger, and the only thing that starts one under Manual.
		// A run in flight is superseded by it. UI thread.
		void runNow();

		// A Run Selection: the part of the stale closure in `targets`' upstream cone, and nothing else —
		// root-level NodeIds, already mapped from what is selected on the level on screen
		// (selectionTargets). Due under every trigger, like Run, and superseding a run in flight; it
		// answers only its own ask, so a change it does not cover still starts a whole run under Live
		// or On commit. Empty targets ask for nothing. UI thread.
		void runSelection(std::vector<lain::flow::NodeId> targets);

		// Bind a root boundary input. Not a document change, and not a write to the working
		// evaluation either, which a run may hold: the value is QUEUED for the next run's start and
		// written into the published copy at once, so the pane binding it reads its own value back
		// this frame rather than snapping back until the run catches up. Then asks for a run, as an
		// edit does. UI thread.
		void bind(lain::flow::PortAddress input, lain::flow::PortValue value);

		// Stop the run in flight and forget every ask for another: what it finished is kept, the rest
		// stays stale until something asks again. UI thread.
		void stopRun();

		// Which scheduler the next run goes through. Parallel by default; remembered per SESSION (the
		// window restores it at startup and writes it back at shutdown).
		RunStrategy strategy() const { return m_strategy; }
		void setStrategy(RunStrategy strategy) { m_strategy = strategy; }

		// Whether a run is in flight, and how far it has got (node computes, summed across stages).
		bool running() const { return m_runner.busy(); }
		std::size_t runPlanned() const { return m_runner.planned(); }
		std::size_t runFinished() const { return m_runner.finished(); }

		// How long the run in flight has been going, in seconds. Meaningful only while running().
		double runElapsed() const { return m_runStarted.since().seconds(); }

		// How the last run that got to an end went — nothing before the first, or since a swap.
		const std::optional<LastRun>& lastRun() const { return m_lastRun; }

		// Why the last run that got to an end threw, when NO NODE owned the throw, until one completes
		// without throwing. A throw a node owned is recorded against that node in the evaluation, which
		// is where the panes show it (Issues lists it, the canvas marks it); this is the rest — a
		// refused prepare, a stage publication — which has nowhere to be shown but the run as a whole. A
		// run that was superseded proves nothing either way, so it does not clear this.
		const std::optional<std::string>& runFailure() const { return m_runFailure; }

		// Frame start: take what the coordinator has produced since the last frame. True when new values
		// landed — a publication, or a node's result folded in — so the caller can refresh what it built
		// from the old ones. UI thread.
		bool pollRun();

		// Frame end: act on the asks, under the document's `trigger` — and reaching as far as they ask
		// (RunRequests::scope: the whole closure, or a Run Selection's cone). `gestureEnded` is the undo
		// history's own boundary (no widget active), so On commit runs on exactly the edits undo
		// records. When a run is due and one is in flight, that one is SUPERSEDED — cancelled, with the
		// asks left standing — and a later frame starts the new one once it has drained; otherwise the
		// document is cloned and the run starts now. So exactly one clone per run that starts, always of
		// the newest document, and the frame loop never waits. Under Manual only Run is ever due, so an
		// edit never cancels a run in flight. UI thread.
		void pumpRun(RunTrigger trigger, bool gestureEnded);

		// Replace the gui-mode scene with a freshly loaded graph (New, Open, an undo/redo restore, a
		// template reload), bind its input to a default gradient so it shows a result, and say it
		// changed. The panes show nothing until the new document's first run publishes — and under
		// Manual that is the next Run, since a swap is a change like any other. Called from the
		// render thread.
		void replaceGraph(std::unique_ptr<lain::flow::Graph> graph);

	private:
		std::uint32_t m_size = 64;			   // example gradient extent (size x size)
		int m_frames = 0;					   // gui-mode: quit after N frames (0 = until closed)
		bool m_useExample = false;			   // gui: --example starts from the example scene, else a blank graph
		bool m_resetLayout = false;			   // gui: --reset-layout ignores the saved dock layout
		std::string m_graphPath;			   // run/list: the graph JSON (empty -> the built-in example scene)
		std::string m_savePath;				   // run --save: serialize the graph here
		lain::core::Range m_frameRange;		   // run --frame: a render over these frames (a TYPED option)
		std::string m_onMissingFrame = "stop"; // run --on-missing-frame: stop | skip
		std::string m_videoCodec = "auto";	   // run --codec: the codec FAMILY, never an encoder name
		lain::media::FrameRate m_outputRate{}; // run --rate: the output rate for a video output
		std::string m_compression = "default"; // run --compression: how hard a still output squeezes
		std::uint8_t m_quality = 0;			   // run --quality: fidelity for a lossy still format

		// The headless subcommands. Their pointers stay valid for the Application's whole lifetime —
		// it OWNS the cli::App rather than parsing through a local, deliberately and because of
		// this — so ->parsed()/->remaining() drive the headless dispatch from onProcess, two phases
		// after onInit handed them over.
		lain::app::cli::App* m_runCmd = nullptr;
		lain::app::cli::App* m_listCmd = nullptr;

		// The gui-mode scene, held by unique_ptr so onStop can release it (and its
		// node-owned payloads) explicitly, before the window/device teardown.
		std::unique_ptr<lain::flow::Graph> m_graph;

		// Its runtime state. The document and this are ONE REPLACEABLE UNIT: an evaluation spans
		// in-place edits and the clones a run reads, but never a rebuilt graph, so replaceGraph swaps
		// both together. SHARED because a run in flight holds it too: a document swapped mid-run drops
		// this reference and the old evaluation drains and dies with its job, and nothing waits.
		// Written only by the coordinator once a job holds it — the UI thread binds into a fresh one
		// in replaceGraph, before any run has seen it, and otherwise goes through bind() below.
		std::shared_ptr<lain::flow::Evaluation> m_evaluation;

		// What the panes read (published()), and the bindings it may not contain yet — queued for the
		// next run, or taken by a run whose publication has not landed.
		lain::flow::PublishedEvaluation m_published;
		std::uint64_t m_publishedBy = 0; // the job whose publication m_published is, results folded in since
		PendingBindings m_bindings;
		RunActivity m_activity;

		RunRequests m_requests; // what has asked for a run and not had one yet
		RunStrategy m_strategy = RunStrategy::Parallel;
		std::optional<std::string> m_runFailure;
		lain::core::Time m_runStarted; // when the run in flight was handed over
		std::optional<LastRun> m_lastRun;

		lain::core::Factory<lain::flow::Node> m_nodeFactory; // node-type palette
		MainWindow m_window;								 // gui-mode inspector

		// LAST, so it is destroyed FIRST: its destructor joins the coordinator, and nothing above may
		// go while a run could still reach it. onStop stops it explicitly anyway, before the pool
		// does; this ordering is the net.
		Runner m_runner;
	};
} // namespace flowview
