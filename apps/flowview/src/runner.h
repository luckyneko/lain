#pragma once

// The gui's side of a run (M14 slice 4, ADR-0025): graph work happens on a COORDINATOR THREAD of its
// own, so the frame loop never waits on it.
//
// Moving Scheduler::run off the frame loop was never only a matter of calling it from another
// thread, because there are two races in the way and not one:
//   * the panes read the evaluation every frame while a run rebinds its slots — so the panes read a
//     PUBLISHED COPY instead (flow::PublishedEvaluation), which this class produces;
//   * a run reads its definition on the workers for its whole length — populateInputs walks the
//     edges, plan steps point into group interiors — while the canvas, the Inspector, the Interface
//     pane and the group sync mutate the document in place. That is the larger one, and undefined
//     behaviour rather than a torn preview. So a run reads a CLONE of the document, taken on the UI
//     thread when the run starts, and edits keep landing on the document itself. A clone keeps every
//     node's id and version, so the one working evaluation stays incremental from one clone to the
//     next.
//
// One job at a time, in one slot. The host starts a job only when the runner is idle and SUPERSEDES
// one in flight by cancelling it and starting the next once it has drained — so it clones exactly
// once per run that actually starts, always from the newest document, and never waits: the drain is
// the coordinator's, and the frame loop just asks again next frame. (Starting the next run before the
// last has drained would need a second working evaluation — ADR-0025's deferred "fork".)
//
// What the frame loop gets back comes through take(): the newest publication, and how the last run
// ended. A publication lands between STAGES (a map's elements, a loop's iterations — through
// RunControl's stage observer) and at the END of every run, a cancelled or failed one included,
// since what a superseded run finished is kept and worth showing.
//
// The coordinator, not the pool: a scheduler run blocks, and entering one from a pool task is a
// deadlock with one pool per process. Under ParallelScheduler this thread is the pool's
// participating "+1" (ADR-0024). The policy of WHEN to run — the trigger — is the host's, not this
// class's (runpolicy.h); so is what a run is fed.

#include "runpolicy.h" // RunStrategy — which scheduler a job goes through

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/portvalue.h>
#include <lain/flow/runcontrol.h>
#include <lain/flow/scheduler.h>
#include <lain/flow/types.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace flowview
{
	// How a run ended.
	enum class RunOutcome
	{
		Completed, // ran its whole closure
		Cancelled, // superseded or stopped: what finished is kept, the rest stays stale
		Failed,	   // something threw: it propagated out of run(), and the node that threw stays stale
	};

	// A PENDING BINDING: a boundary value the host has bound that no run has consumed yet. Applied by
	// the coordinator at the start of the next run, after it has prepared the evaluation — so a pin
	// added since the last run already has the slot the value lands in.
	using Binding = std::pair<lain::flow::PortAddress, lain::flow::PortValue>;

	// Everything one run needs. The job SHARES the clone and the working evaluation, so a host that
	// replaces its document mid-run (New, Open, undo) simply drops its own references: the old pair
	// drains and is destroyed with the job, on the coordinator, and nothing waits.
	struct RunJob
	{
		std::shared_ptr<const lain::flow::Graph> definition; // a clone of the document, taken on the UI thread
		std::shared_ptr<lain::flow::Evaluation> evaluation;	 // the working evaluation, of the same lineage
		std::vector<Binding> bindings;						 // the latest value per pin
		RunStrategy strategy = RunStrategy::Parallel;
	};

	// What the frame loop takes each frame. Each part is present only if something landed since the
	// last take.
	struct RunReport
	{
		std::optional<lain::flow::PublishedEvaluation> published; // the newest, and only the newest
		std::uint64_t publishedBy = 0;							  // the job that published it (start()'s answer)
		std::optional<RunOutcome> outcome;						  // a run returned
		std::string failure;									  // Failed only: what the exception said
		// With the outcome: how many node computes THREW (flow::RunControl::failed()). Each of those is
		// recorded against its node in the evaluation, which is where a host shows it; a Failed run
		// with none threw from somewhere no node owns — a refused prepare, a stage observer — and only
		// that is worth reporting for the run as a whole.
		std::size_t failedNodes = 0;
	};

	// The host's pending bindings, and what it takes to keep SHOWING them until a publication includes
	// them (ADR-0025's "shown at once").
	//
	// A value is queued here when bound, handed to the next job when one starts, and applied by that
	// job after its prepare. Until then no publication contains it — and neither does a publication
	// from an EARLIER job that happens to land after the hand-over, which is not a corner case: a run
	// that finishes between the frame's poll and its pump publishes into the next frame, after the
	// next job has already taken the queue. Showing a publication as it came would read the old value
	// back for a frame, and a scalar drag would snap back. So each binding remembers which job took
	// it, and is laid over any publication from a job before that one.
	class PendingBindings
	{
	public:
		// Queue a value for `input`. The latest per pin is what counts: a drag binds every frame.
		void set(lain::flow::PortAddress input, lain::flow::PortValue value);

		// The values `job` should apply — everything queued since the last hand-over — now remembered
		// as taken by it.
		std::vector<Binding> handOver(std::uint64_t job);

		// Lay every binding that `published` cannot contain over it: those taken by a job after
		// `publishedBy`, and those not taken yet. A binding a publication does contain is forgotten,
		// since from here on every publication will.
		void showOn(lain::flow::PublishedEvaluation& published, std::uint64_t publishedBy);

		// A document swap: none of them addresses the new document.
		void clear() { m_entries.clear(); }

	private:
		struct Entry
		{
			Binding binding;
			std::uint64_t job = 0; // 0 while queued; the job that took it afterwards
		};
		std::vector<Entry> m_entries; // oldest first, so laying them over in order leaves the newest
	};

	class Runner
	{
	public:
		// The coordinator thread starts with the first job, so a headless process — which never
		// starts one — never has it.
		Runner() = default;
		~Runner(); // stop()
		Runner(const Runner&) = delete;
		Runner& operator=(const Runner&) = delete;

		// A job is waiting to start, or running.
		bool busy() const;

		// The serial the next start() will answer — so a host can hand a job bindings tagged with the
		// job that will apply them before handing over the job itself.
		std::uint64_t nextJob() const;

		// Hand the coordinator a job, and answer its serial (1, 2, 3 ...), which is what a
		// publication is later tagged with. PRECONDITION: !busy() — the host supersedes by cancelling
		// and starting again once the runner has drained, never by queueing a second job behind the
		// first. The job's RunControl is made HERE, so a cancel that arrives before the coordinator has
		// picked the job up still applies to it.
		std::uint64_t start(RunJob job);

		// Stop the run in flight — a supersede, or the Stop command. It stops at its next step
		// boundary (flow::RunControl); what it finished is kept, and it still publishes when it ends.
		void cancel();

		// Cancel, AND drop anything the run would still publish or report: its document has been
		// replaced, and what it computed belongs to a lineage the panes are no longer showing.
		void abandon();

		// Progress of the current (or last) run: node computes planned and finished, summed across
		// stages. Zero before the first run.
		std::size_t planned() const;
		std::size_t finished() const;

		// UI thread, once a frame: what landed since the last take.
		RunReport take();

		// Abandon, and JOIN the coordinator. Idempotent. A host calls this before it releases anything
		// a run might reach — the task pool above all, since a parallel run participates in it.
		void stop();

	private:
		void coordinate(); // the coordinator thread's body

		// Run one job to its end, never throwing: an exception becomes the outcome.
		RunOutcome execute(RunJob& job, std::uint64_t serial, lain::flow::RunControl& control, std::string& failure);

		// Copy the job's evaluation into the slot the frame loop takes from. A STAGE publication skips
		// the copy while the previous one is still untaken, so a hundred-iteration loop costs at most
		// about one copy per frame; the END publication always replaces what is there.
		void publish(const RunJob& job, std::uint64_t serial, bool final);

		mutable std::mutex m_mutex;
		std::condition_variable m_wake;

		std::optional<RunJob> m_job;					   // handed over, not yet picked up
		std::uint64_t m_lastJob = 0;					   // the serial of the last job started
		std::shared_ptr<lain::flow::RunControl> m_control; // the current (or last) job's
		bool m_busy = false;
		bool m_abandoned = false; // the current job publishes nothing more
		bool m_stopping = false;

		std::optional<lain::flow::PublishedEvaluation> m_published;
		std::uint64_t m_publishedBy = 0;
		std::optional<RunOutcome> m_outcome;
		std::string m_failure;
		std::size_t m_failedNodes = 0;

		// Stateless, so one of each serves every run.
		lain::flow::SerialScheduler m_serial;
		lain::flow::ParallelScheduler m_parallel;

		std::thread m_thread;
	};
} // namespace flowview
